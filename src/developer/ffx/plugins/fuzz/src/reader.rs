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
    std::path::{Path, PathBuf},
    std::sync::{Arc, Mutex},
    std::thread,
    termion::{color, style},
};

/// Trait for getting input such as user commands.
///
/// `Reader` provides a way to abstract the source of user input to the `ffx fuzz` plugin.
#[async_trait(?Send)]
pub trait Reader {
    /// Begin reading. Must be called at most once. Does nothing by default.
    fn start<P: AsRef<Path>>(&mut self, _fuchsia_dir: P, _state: Arc<Mutex<FuzzerState>>) {}

    /// Read and parse a command.
    async fn prompt(&mut self) -> Option<ParsedCommand>;

    /// Returns past unparsed inputs. Returns an empty vector by default.
    fn history(&self) -> Vec<String> {
        Vec::new()
    }

    /// Returns a future that waits until a newline is entered.
    async fn until_enter(&mut self) -> Result<()>;
}

/// Represents the result of parsing user input.
#[derive(Debug)]
pub enum ParsedCommand {
    Empty,
    Invalid(String),
    Usage(String),
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
    async fn until_enter(&mut self) -> Result<()> {
        bail!("not supported.")
    }
}

/// The ShellReader represents input from a user using the interactive fuzzing shell.
pub struct ShellReader {
    prompt_sender: mpsc::UnboundedSender<String>,
    prompt_receiver: Option<mpsc::UnboundedReceiver<String>>,
    parsed_sender: Option<mpsc::UnboundedSender<ParsedCommand>>,
    parsed_receiver: mpsc::UnboundedReceiver<ParsedCommand>,
    history: Arc<Mutex<Vec<String>>>,
}

impl ShellReader {
    /// Creates a `ShellReader`.
    ///
    /// When `prompt` is called, this object will query to user for a command.
    pub fn new() -> Self {
        let (prompt_sender, prompt_receiver) = mpsc::unbounded::<String>();
        let (parsed_sender, parsed_receiver) = mpsc::unbounded::<ParsedCommand>();
        Self {
            prompt_sender,
            prompt_receiver: Some(prompt_receiver),
            parsed_sender: Some(parsed_sender),
            parsed_receiver,
            history: Arc::new(Mutex::new(Vec::new())),
        }
    }
}

#[async_trait(?Send)]
impl Reader for ShellReader {
    fn start<P: AsRef<Path>>(&mut self, fuchsia_dir: P, state: Arc<Mutex<FuzzerState>>) {
        let fuchsia_dir = PathBuf::from(fuchsia_dir.as_ref());
        let prompt_receiver =
            self.prompt_receiver.take().expect("ShellReader::start called more than once");
        let parsed_sender =
            self.parsed_sender.take().expect("ShellReader::start called more than once");
        let history = Arc::clone(&self.history);
        // Set up a thread for forwarding stdin. Reading from stdin is a blocking operation which
        // will halt the executor if it were to run on the same thread.
        thread::spawn(move || {
            let mut executor = fasync::LocalExecutor::new().expect("Failed to create executor");
            executor.run_singlethreaded(async move {
                shell_read_loop(fuchsia_dir, prompt_receiver, parsed_sender, state, history).await;
            });
        });
    }

    async fn prompt(&mut self) -> Option<ParsedCommand> {
        let prompt = format!(
            "{reset}{bold}{yellow_fg}fuzz »{reset} ",
            yellow_fg = color::Fg(color::Yellow),
            bold = style::Bold,
            reset = style::Reset,
        );
        self.prompt_sender.send(prompt).await.expect("Failed to prompt for input");
        self.parsed_receiver.next().await
    }

    fn history(&self) -> Vec<String> {
        let history = self.history.lock().unwrap();
        history.clone()
    }

    async fn until_enter(&mut self) -> Result<()> {
        self.prompt_sender.send(String::default()).await.expect("Failed to prompt for interrupt");
        self.parsed_receiver.next().await.map_or(Err(anyhow!("input closed")), |_| Ok(()))
    }
}

async fn shell_read_loop(
    fuchsia_dir: PathBuf,
    mut prompt_receiver: mpsc::UnboundedReceiver<String>,
    mut parsed_sender: mpsc::UnboundedSender<ParsedCommand>,
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
    editor.set_helper(Some(FuzzHelper::new(&fuchsia_dir, state)));
    while let Some(prompt) = prompt_receiver.next().await {
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
        parsed_sender.send(parsed).await.expect("Failed to send parsed input");
    }
}

#[cfg(test)]
pub mod test_fixtures {
    use {
        super::{parse, ParsedCommand, Reader},
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
        interrupt_sender: mpsc::UnboundedSender<fasync::Duration>,
        interrupt_receiver: mpsc::UnboundedReceiver<fasync::Duration>,
    }

    impl ScriptReader {
        /// Creates a `ScriptReader`.
        ///
        /// This object is similar to `CommandReader` except that it allows commands to be `add`ed
        /// to by unit test after its creation.
        pub fn new() -> Self {
            let (interrupt_sender, interrupt_receiver) = mpsc::unbounded::<fasync::Duration>();
            Self { commands: LinkedList::new(), interrupt_sender, interrupt_receiver }
        }

        /// Adds a scripted command to be returned by `prompt`.
        pub fn add<S: AsRef<str>>(&mut self, command: S) {
            self.commands.push_back(command.as_ref().to_string());
        }

        /// Indicates a call to `until_enter` should return `after` a certain duration.
        pub async fn interrupt(&mut self, after: fasync::Duration) {
            self.interrupt_sender.send(after).await.expect("failed to send interrupt");
        }
    }

    #[async_trait(?Send)]
    impl Reader for ScriptReader {
        async fn prompt(&mut self) -> Option<ParsedCommand> {
            self.commands.pop_front().and_then(|command| Some(parse(&command)))
        }

        async fn until_enter(&mut self) -> Result<()> {
            match self.interrupt_receiver.next().await {
                Some(delay) => fasync::Timer::new(delay).await,
                None => bail!("receiver closed"),
            };
            Ok(())
        }
    }
}
