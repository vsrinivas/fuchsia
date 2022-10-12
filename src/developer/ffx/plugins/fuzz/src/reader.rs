// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::autocomplete::FuzzHelper,
    anyhow::{anyhow, bail, Result},
    argh::FromArgs,
    async_trait::async_trait,
    ffx_fuzz_args::{FuzzShellCommand, FuzzerState},
    fuchsia_async as fasync,
    futures::channel::mpsc,
    futures::{SinkExt, StreamExt},
    rustyline::error::ReadlineError,
    rustyline::{CompletionType, Config, Editor},
    std::io,
    std::sync::{Arc, Mutex},
    std::thread,
    termion::input::TermRead,
    termion::raw::IntoRawMode,
    termion::{color, style},
};

/// Trait for getting input such as user commands.
///
/// `Reader` provides a way to abstract the source of user input to the `ffx fuzz` plugin.
#[async_trait(?Send)]
pub trait Reader {
    /// Returns whether this reader interactively prompts a user.
    fn is_interactive(&self) -> bool {
        false
    }

    /// Begin reading. Must be called at most once. Does nothing by default.
    fn start(&mut self, _tests_json: Option<String>, _state: Arc<Mutex<FuzzerState>>) {}

    /// Read and parse a command.
    async fn prompt(&mut self) -> Option<ParsedCommand>;

    /// Returns a future that waits until output has been paused and resumed.
    async fn pause(&mut self) -> Result<()>;

    /// Returns past unparsed inputs. Returns an empty vector by default.
    fn history(&self) -> Vec<String> {
        Vec::new()
    }
}

/// Represents the result of parsing user input.
#[derive(Debug)]
pub enum ParsedCommand {
    Empty,
    Invalid(String),
    Usage(String),
    Pause,
    Valid(FuzzShellCommand),
}

fn parse(cmdline: &str) -> ParsedCommand {
    let cmdline = cmdline.trim().to_string();
    if cmdline.is_empty() || cmdline.starts_with("#") {
        return ParsedCommand::Empty;
    }
    let tokens: Vec<&str> = cmdline.split_whitespace().collect();
    let (output, status) = match FuzzShellCommand::from_args(&[""], tokens.as_slice()) {
        Ok(command) => return ParsedCommand::Valid(command),
        Err(argh::EarlyExit { output, status }) => (output, status),
    };
    match status {
        Ok(_) => ParsedCommand::Usage(output),
        Err(_) => ParsedCommand::Invalid(output),
    }
}

/// The CommandReader represents input from a fixed set of commands, i.e. no input at all.
pub struct CommandReader {
    iter: Box<dyn Iterator<Item = FuzzShellCommand>>,
}

impl CommandReader {
    /// Creates a `CommandReader`.
    ///
    /// When `prompt` is called, this object will return the given `commands`, one at a time.
    pub fn new(commands: Vec<FuzzShellCommand>) -> Self {
        Self { iter: Box::new(commands.into_iter()) }
    }
}

#[async_trait(?Send)]
impl Reader for CommandReader {
    // Fixed commands don't prompt for additional commands.
    async fn prompt(&mut self) -> Option<ParsedCommand> {
        self.iter.next().and_then(|command| Some(ParsedCommand::Valid(command)))
    }

    // Not interruptible.
    async fn pause(&mut self) -> Result<()> {
        bail!("not supported.")
    }
}

/// The ShellReader represents input from a user using the interactive fuzzing shell.
pub struct ShellReader {
    sender: mpsc::UnboundedSender<InputRequest>,
    receiver: mpsc::UnboundedReceiver<ParsedCommand>,
    history: Arc<Mutex<Vec<String>>>,
}

enum InputRequest {
    Command,
    AnyKey,
}

impl ShellReader {
    /// Creates a `ShellReader`.
    ///
    /// When `prompt` is called, this object will query to user for a command.
    pub fn new() -> Self {
        let (sender, _) = mpsc::unbounded::<InputRequest>();
        let (_, receiver) = mpsc::unbounded::<ParsedCommand>();
        Self { sender, receiver, history: Arc::new(Mutex::new(Vec::new())) }
    }
}

#[async_trait(?Send)]
impl Reader for ShellReader {
    fn is_interactive(&self) -> bool {
        true
    }

    fn start(&mut self, tests_json: Option<String>, state: Arc<Mutex<FuzzerState>>) {
        let (req_sender, req_receiver) = mpsc::unbounded::<InputRequest>();
        let (resp_sender, resp_receiver) = mpsc::unbounded::<ParsedCommand>();
        self.sender = req_sender;
        self.receiver = resp_receiver;
        let history = Arc::clone(&self.history);
        // Set up a thread for forwarding stdin. Reading from stdin is a blocking operation which
        // will halt the executor if it were to run on the same thread.
        thread::spawn(move || {
            let mut executor = fasync::LocalExecutor::new().expect("Failed to create executor");
            executor.run_singlethreaded(async move {
                shell_read_loop(tests_json, req_receiver, resp_sender, state, history).await;
            });
        });
    }

    async fn prompt(&mut self) -> Option<ParsedCommand> {
        self.sender.send(InputRequest::Command).await.expect("Failed to prompt for input");
        self.receiver.next().await
    }

    async fn pause(&mut self) -> Result<()> {
        self.sender.send(InputRequest::AnyKey).await.expect("Failed to prompt for interrupt");
        self.receiver.next().await.map_or(Err(anyhow!("input closed")), |_| Ok(()))
    }

    fn history(&self) -> Vec<String> {
        let history = self.history.lock().unwrap();
        history.clone()
    }
}

async fn shell_read_loop(
    tests_json: Option<String>,
    mut receiver: mpsc::UnboundedReceiver<InputRequest>,
    mut sender: mpsc::UnboundedSender<ParsedCommand>,
    state: Arc<Mutex<FuzzerState>>,
    history: Arc<Mutex<Vec<String>>>,
) {
    let config =
        Config::builder().history_ignore_space(true).completion_type(CompletionType::List).build();
    let mut editor = Editor::with_config(config);
    let _ = editor.load_history("/tmp/fuzz_history");
    {
        let mut history_mut = history.lock().unwrap();
        *history_mut = editor.history().iter().map(|s| s.clone()).collect();
    }
    editor.set_helper(Some(FuzzHelper::new(tests_json, state)));
    while let Some(input_request) = receiver.next().await {
        match input_request {
            InputRequest::Command => {
                let prompt = format!(
                    "{reset}{bold}{yellow_fg}fuzz »{reset} ",
                    yellow_fg = color::Fg(color::Yellow),
                    bold = style::Bold,
                    reset = style::Reset,
                );
                let parsed = match editor.readline(&prompt) {
                    Ok(line) => match parse(&line) {
                        ParsedCommand::Empty => ParsedCommand::Empty,
                        other => {
                            editor.add_history_entry(&line);
                            let _ = editor.save_history("/tmp/fuzz_history");
                            let mut history_mut = history.lock().unwrap();
                            history_mut.push(line);
                            other
                        }
                    },
                    Err(ReadlineError::Eof) => break,
                    Err(ReadlineError::Interrupted) => break,
                    Err(e) => panic!("Failed to read input: {}", e),
                };
                sender.send(parsed).await.expect("Failed to send parsed input");
            }
            InputRequest::AnyKey => {
                let _raw = io::stdout().into_raw_mode().expect("Failed to set terminal raw mode");
                io::stdin().keys().next();
                sender.send(ParsedCommand::Pause).await.expect("Failed to send pause signal");
            }
        }
    }
}

#[cfg(test)]
pub mod test_fixtures {
    use {
        super::{parse, ParsedCommand, Reader},
        crate::duration::{deadline_after, Duration},
        anyhow::{bail, Result},
        async_trait::async_trait,
        fuchsia_async as fasync,
        futures::channel::mpsc,
        futures::{SinkExt, StreamExt},
        std::collections::LinkedList,
    };

    /// The ScriptReader represents canned input for a unit test.
    pub struct ScriptReader {
        commands: LinkedList<String>,
        sender: mpsc::UnboundedSender<Duration>,
        receiver: mpsc::UnboundedReceiver<Duration>,
    }

    impl ScriptReader {
        /// Creates a `ScriptReader`.
        ///
        /// This object is similar to `CommandReader` except that it allows commands to be `add`ed
        /// to by unit test after its creation.
        pub fn new() -> Self {
            let (sender, receiver) = mpsc::unbounded::<Duration>();
            Self { commands: LinkedList::new(), sender, receiver }
        }

        /// Adds a scripted command to be returned by `prompt`.
        pub fn add<S: AsRef<str>>(&mut self, command: S) {
            self.commands.push_back(command.as_ref().to_string());
        }

        /// Indicates a call to `until_enter` should return `after` a certain duration.
        pub async fn interrupt(&mut self, after: Duration) {
            self.sender.send(after).await.expect("failed to send interrupt");
        }
    }

    #[async_trait(?Send)]
    impl Reader for ScriptReader {
        /// Claims to be interactive (although it is not) to get all potential output.
        fn is_interactive(&self) -> bool {
            true
        }

        async fn prompt(&mut self) -> Option<ParsedCommand> {
            self.commands.pop_front().and_then(|command| Some(parse(&command)))
        }

        async fn pause(&mut self) -> Result<()> {
            let timeout = self.receiver.next().await;
            let timeout = timeout.map(|duration| duration.into_nanos());
            match deadline_after(timeout) {
                Some(deadline) => fasync::Timer::new(deadline).await,
                None => bail!("receiver closed"),
            };
            Ok(())
        }
    }
}
