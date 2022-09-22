// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::corpus::get_type as get_corpus_type,
    crate::fuzzer::Fuzzer,
    crate::manager::Manager,
    crate::reader::{ParsedCommand, Reader},
    crate::util::get_fuzzer_urls,
    crate::writer::{OutputSink, Writer},
    anyhow::{bail, Context as _, Result},
    errors::ffx_bail,
    ffx_fuzz_args::*,
    fidl_fuchsia_developer_remotecontrol as rcs, fidl_fuchsia_fuzzer as fuzz,
    fuchsia_async as fasync, fuchsia_zircon_status as zx,
    futures::{pin_mut, select, FutureExt},
    serde_json::json,
    std::cell::RefCell,
    std::convert::TryInto,
    std::fs,
    std::sync::{Arc, Mutex},
    termion::{self, clear, cursor},
    url::Url,
};

/// The default output directory variable used by `Shell::attach`.
pub const DEFAULT_FUZZING_OUTPUT_VARIABLE: &str = "fuzzer.output";

/// Interactive fuzzing shell.
pub struct Shell<R: Reader, O: OutputSink> {
    tests_json: Option<String>,
    rc: rcs::RemoteControlProxy,
    state: Arc<Mutex<FuzzerState>>,
    fuzzer: RefCell<Option<Fuzzer<O>>>,
    reader: RefCell<R>,
    writer: Writer<O>,
}

/// Indicates what the shell should do after trying to execute a command.
#[derive(Debug, PartialEq)]
pub enum NextAction {
    /// An `execute_*` subroutine did not handle the command; the next candidate should be tried.
    Retry(FuzzShellCommand),

    /// The command was handled, and the user should be prompted for the next command.
    Prompt,

    /// The fuzzer output was previously paused, and should be resumed.
    Resume,

    /// The command was handled as a user request to exit the shell.
    Exit,
}

impl<R: Reader, O: OutputSink> Shell<R, O> {
    /// Creates a shell that executes a sequence of fuzzer-related commands.
    ///
    /// The shell may be interactive or scripted, depending on its `reader`. It will produce output
    /// using its `writer`. The fuzzers available for the shell to interact with are discovered
    /// by examining the `tests_json`.
    pub fn new(
        tests_json: Option<String>,
        rc: rcs::RemoteControlProxy,
        mut reader: R,
        writer: &Writer<O>,
    ) -> Self {
        let state = Arc::new(Mutex::new(FuzzerState::Detached));
        reader.start(tests_json.clone(), Arc::clone(&state));
        Self {
            tests_json,
            rc,
            state,
            fuzzer: RefCell::new(None),
            reader: RefCell::new(reader),
            writer: writer.clone(),
        }
    }

    /// Runs a blocking loop executing commands from the `reader`.
    pub async fn run(&mut self) -> Result<()> {
        let is_interactive = self.reader.borrow().is_interactive();
        loop {
            if let Some(command) = self.prompt().await {
                match self.execute(command).await {
                    Ok(NextAction::Prompt) => {}
                    Ok(NextAction::Exit) => break,
                    Err(e) => {
                        let err_msg = format!("failed to execute command: {:?}", e);
                        if is_interactive {
                            self.writer.error(err_msg);
                        } else {
                            ffx_bail!("{}", err_msg);
                        }
                    }
                    _ => unreachable!(),
                };
            };
        }
        Ok(())
    }

    async fn prompt(&self) -> Option<FuzzShellCommand> {
        let parsed = {
            let mut reader = self.reader.borrow_mut();
            reader.prompt().await
        };
        match parsed {
            Some(ParsedCommand::Empty) | Some(ParsedCommand::Pause) => None,
            Some(ParsedCommand::Invalid(output)) => {
                self.writer.error(output);
                self.writer.println("Command is unrecognized or invalid for the current state.");
                self.writer.println("Try 'help' to list recognized.");
                self.writer.println("Try 'status' to check the current state.");
                None
            }
            Some(ParsedCommand::Usage(output)) => {
                self.writer.println(output);
                None
            }
            Some(ParsedCommand::Valid(command)) => Some(command),
            None => Some(FuzzShellCommand {
                command: FuzzShellSubcommand::Exit(ExitShellSubcommand {}),
            }),
        }
    }

    /// Executes a single command.
    /// Never returns `NextAction::Retry(_)`.
    pub async fn execute(&self, args: FuzzShellCommand) -> Result<NextAction> {
        // Dispatch based on current fuzzer state.
        let fuzzer = self.get_fuzzer();
        let next_action = match (self.get_state(), fuzzer.as_ref()) {
            (FuzzerState::Detached, None) => self.execute_detached(args).await,
            (FuzzerState::Idle, Some(fuzzer)) => self.execute_idle(args, fuzzer).await,
            (FuzzerState::Running, Some(fuzzer)) => self.execute_running(args, fuzzer).await,
            (state, fuzzer) => {
                unreachable!("invalid state: ({:?}, {:?}) for {:?}", state, fuzzer, args)
            }
        };
        // Replace the fuzzer unless the shell detached. Execution may have changed state.
        if let Some(fuzzer) = fuzzer {
            // If we encountered an error, try to stop the fuzzer.
            if next_action.is_err() {
                let url = fuzzer.url();
                let _ = self.stop(&url.to_string()).await;
            }
            match self.get_state() {
                FuzzerState::Detached => {}
                FuzzerState::Idle | FuzzerState::Running => self.put_fuzzer(fuzzer),
            };
        }
        next_action
    }

    /// Handles commands that can be run in any state.
    /// May return `NextAction::Retry(_)`.
    async fn execute_any(&self, args: FuzzShellCommand) -> Result<NextAction> {
        match args.command {
            FuzzShellSubcommand::List(ListSubcommand { json_file, pattern }) => {
                let tests_json = json_file.or(self.tests_json.clone());
                let mut urls =
                    get_fuzzer_urls(&tests_json).context("failed to get URLs to list")?;
                if let Some(pattern) = pattern {
                    let globbed = glob::Pattern::new(&pattern)
                        .context("failed to create glob from pattern")?;
                    urls.retain(|url| globbed.matches(url.as_str()));
                }
                let urls: Vec<_> = urls.into_iter().map(|url| url.to_string()).collect();
                let urls = json!(urls);
                let fuzzers = serde_json::to_string_pretty(&urls)?;
                self.writer.println(fuzzers);
            }
            FuzzShellSubcommand::Clear(ClearShellSubcommand {}) => {
                self.writer.print(format!("{}{}", clear::All, cursor::Goto(1, 1)));
            }
            FuzzShellSubcommand::History(HistoryShellSubcommand {}) => {
                let mut count = 1;
                let history = self.reader.borrow().history();
                for entry in history.into_iter() {
                    self.writer.println(format!("{} {}", count, entry));
                    count += 1;
                }
            }
            _ => return Ok(NextAction::Retry(args)),
        };
        Ok(NextAction::Prompt)
    }

    /// Handles commands that can only be run when no fuzzer is attached.
    /// Never returns `NextAction::Retry(_)`.
    async fn execute_detached(&self, args: FuzzShellCommand) -> Result<NextAction> {
        let args = match self.execute_any(args).await {
            Ok(NextAction::Retry(args)) => args,
            other => return other,
        };
        match args.command {
            FuzzShellSubcommand::Attach(AttachShellSubcommand { url, output }) => {
                self.attach(&url, output).await.context("failed to attach fuzzer")
            }
            FuzzShellSubcommand::Status(StatusShellSubcommand {}) => {
                self.writer.println("No fuzzer attached.");
                Ok(NextAction::Prompt)
            }
            FuzzShellSubcommand::Stop(StopSubcommand { url, .. }) => {
                match url {
                    Some(url) => self.stop(&url).await.context("failed to stop fuzzer")?,
                    None => self.writer.error("invalid command: no fuzzer attached."),
                };
                Ok(NextAction::Prompt)
            }
            FuzzShellSubcommand::Exit(ExitShellSubcommand {}) => {
                if self.reader.borrow().is_interactive() {
                    self.writer.println("Exiting...");
                }
                Ok(NextAction::Exit)
            }
            _ => {
                self.writer.error("invalid command: no fuzzer attached.");
                Ok(NextAction::Prompt)
            }
        }
    }

    /// Handles commands that can be run when a fuzzer is idle or running.
    /// May return `NextAction::Retry(_)`.
    async fn execute_attached(
        &self,
        args: FuzzShellCommand,
        fuzzer: &Fuzzer<O>,
    ) -> Result<NextAction> {
        let args = match self.execute_any(args).await {
            Ok(NextAction::Retry(args)) => args,
            other => return other,
        };
        match args.command {
            FuzzShellSubcommand::Attach(AttachShellSubcommand { .. }) => {
                self.writer.error("invalid command: a fuzzer is already attached.");
            }
            FuzzShellSubcommand::Get(GetShellSubcommand { name }) => {
                fuzzer.get(name).await.context("failed to get option for fuzzer")?;
            }
            FuzzShellSubcommand::Add(AddShellSubcommand { input, seed }) => {
                fuzzer
                    .add(input, get_corpus_type(seed))
                    .await
                    .context("failed to add fuzz input to fuzzer corpus")?;
            }
            FuzzShellSubcommand::Status(StatusShellSubcommand {}) => {
                self.display_status(fuzzer).await.context("failed to display status for fuzzer")?;
            }
            FuzzShellSubcommand::Fetch(FetchShellSubcommand { seed }) => {
                let corpus_type = get_corpus_type(seed);
                fuzzer.fetch(corpus_type).await.context("failed to fetch corpus from fuzzer")?;
            }
            FuzzShellSubcommand::Detach(DetachShellSubcommand {}) => {
                self.writer.println(format!("Detaching from '{}'...", fuzzer.url()));
                self.detach();
                self.writer.println(format!("Detached."));
            }
            FuzzShellSubcommand::Stop(StopSubcommand { url, .. }) => {
                let url = url.unwrap_or(fuzzer.url().to_string());
                self.stop(&url).await.context("failed to stop fuzzer")?;
            }
            FuzzShellSubcommand::Exit(ExitShellSubcommand {}) => {
                self.writer.println("Exiting...");
                self.detach();
                return Ok(NextAction::Exit);
            }
            _ => return Ok(NextAction::Retry(args)),
        };
        Ok(NextAction::Prompt)
    }

    /// Handles commands that can only be run when a fuzzer is idle.
    /// Never returns `NextAction::Retry(_)`.
    async fn execute_idle(&self, args: FuzzShellCommand, fuzzer: &Fuzzer<O>) -> Result<NextAction> {
        let args = match self.execute_attached(args, fuzzer).await {
            Ok(NextAction::Retry(args)) => args,
            other => return other,
        };

        match args.command {
            FuzzShellSubcommand::Set(SetShellSubcommand { name, value }) => {
                fuzzer.set(&name, &value).await.context("failed to set option on fuzzer")?;
                return Ok(NextAction::Prompt);
            }
            FuzzShellSubcommand::Try(_)
            | FuzzShellSubcommand::Run(_)
            | FuzzShellSubcommand::Cleanse(_)
            | FuzzShellSubcommand::Minimize(_)
            | FuzzShellSubcommand::Merge(_) => {}
            _ => {
                self.writer.error("invalid command: no fuzzer running.");
                return Ok(NextAction::Prompt);
            }
        };

        let is_interactive = self.reader.borrow().is_interactive();
        if is_interactive {
            self.writer.println("Starting workflow...");
            self.writer.println("Press any key to pause fuzzer output.");
        }
        self.set_state(FuzzerState::Running);
        let workflow_fut = || async move {
            let result = match args.command {
                FuzzShellSubcommand::Try(TryShellSubcommand { input }) => {
                    fuzzer.try_one(input).await
                }
                FuzzShellSubcommand::Run(RunShellSubcommand { runs, time }) => {
                    fuzzer.run(runs, time).await
                }
                FuzzShellSubcommand::Cleanse(CleanseShellSubcommand { input }) => {
                    fuzzer.cleanse(input).await
                }
                FuzzShellSubcommand::Minimize(MinimizeShellSubcommand { input, runs, time }) => {
                    fuzzer.minimize(input, runs, time).await
                }
                FuzzShellSubcommand::Merge(MergeShellSubcommand {}) => fuzzer.merge().await,
                _ => unreachable!(),
            };
            if self.get_state() == FuzzerState::Running {
                self.set_state(FuzzerState::Idle);
            }
            if is_interactive {
                // If an interactive workflow completed successfully, then the `rustyline`
                // editor is sitting in a blocking, uncancellable read of stdin at this
                // point. Cancelling that read would require reworking `rustyline` to reduce
                // its portability and include unsafe code to perform use a *nix epoll API
                // directly. The simpler solution is just to have the user press a key.
                if result.as_ref().ok() == Some(&zx::Status::OK) {
                    self.writer.println("Workflow complete. Press any key to continue...");
                } else if result.is_err() {
                    self.writer.println("Workflow failed. Press any key to continue...");
                }
            };
            result
        };
        let workflow_fut = workflow_fut().fuse();
        let pause_fut = self.handle_pause(fuzzer).fuse();
        pin_mut!(workflow_fut, pause_fut);
        loop {
            select! {
                result = workflow_fut => {
                    result?;
                }
                next_action = pause_fut => {
                    if next_action != NextAction::Resume {
                        return Ok(next_action);
                    }
                }
                complete => return Ok(NextAction::Prompt),
            }
        }
    }

    /// Handles commands that can only be run when a fuzzer is running.
    /// Never returns `NextAction::Retry(_)`.
    async fn execute_running(
        &self,
        args: FuzzShellCommand,
        fuzzer: &Fuzzer<O>,
    ) -> Result<NextAction> {
        let args = match self.execute_attached(args, fuzzer).await {
            Ok(NextAction::Retry(args)) => args,
            other => return other,
        };
        match args.command {
            FuzzShellSubcommand::Resume(ResumeShellSubcommand {}) => {
                self.writer.println("Resuming fuzzer output...");
                self.writer.println("Press any key to pause fuzzer output.");
                self.writer.resume();
                return Ok(NextAction::Resume);
            }
            _ => {
                self.writer.error("invalid command: a long-running workflow is in progress.");
            }
        };
        Ok(NextAction::Prompt)
    }

    // Subroutines used by the execution routines above.

    // Connects to a fuzzer given by the `url`.
    async fn attach(&self, url: &str, output: Option<String>) -> Result<NextAction> {
        let url = Url::parse(url).context("invalid fuzzer URL")?;
        let output = match (output, ffx_config::get(DEFAULT_FUZZING_OUTPUT_VARIABLE).await) {
            (Some(output), _) | (None, Ok(output)) => output,
            _ => {
                self.writer.error("output directory is not set.");
                self.writer.println("You can specify the location with the `--output` option.");
                self.writer.println("You can also set a default output directory using:");
                self.writer.println(format!(
                    "  `ffx config set {} <path>`",
                    DEFAULT_FUZZING_OUTPUT_VARIABLE
                ));
                return Ok(NextAction::Prompt);
            }
        };
        let metadata = fs::metadata(&output)
            .with_context(|| format!("invalid output directory: '{}'", output))?;
        if !metadata.is_dir() {
            bail!("not a directory: '{}'", output);
        }
        if metadata.permissions().readonly() {
            bail!("output directory is read-only: '{}'", output);
        }

        // Pre-emptively pause, and then resume if the fuzzer is currently running.
        self.writer.pause();

        self.writer.println(format!("Attaching to '{}'...", url));
        let manager = Manager::with_remote_control(&self.rc, &self.writer)
            .await
            .context("failed to connect to manager")?;
        let fuzzer = manager.connect(url, output).await.context("failed to connect to fuzzer")?;
        let status = fuzzer.status().await.context("failed to get status from fuzzer")?;
        self.put_fuzzer(fuzzer);
        match status.running {
            Some(true) => {
                self.set_state(FuzzerState::Running);
                self.writer.println("Attached; fuzzer is running.");
                self.writer.println("Fuzzer output has been paused.");
                self.writer.println("To resume output, use the `resume` command.");
            }
            _ => {
                self.set_state(FuzzerState::Idle);
                self.writer.resume();
                self.writer.println("Attached; fuzzer is idle.");
            }
        };
        Ok(NextAction::Prompt)
    }

    // Returns status from the currently attached fuzzer, and updates the shell state accordingly.
    async fn get_status(&self, fuzzer: &Fuzzer<O>) -> Result<fuzz::Status> {
        let status = fuzzer.status().await.context("failed to get status from fuzzer")?;
        match status.running {
            Some(true) => self.set_state(FuzzerState::Running),
            _ => self.set_state(FuzzerState::Idle),
        };
        Ok(status)
    }

    // Prints status information from an attached fuzzer.
    async fn display_status(&self, fuzzer: &Fuzzer<O>) -> Result<()> {
        let status = self.get_status(fuzzer).await.context("failed to get status to display")?;
        match self.get_state() {
            FuzzerState::Idle => {
                self.writer.println(format!("{} is idle.", fuzzer.url()));
            }
            FuzzerState::Running => {
                self.writer.println(format!("{} is running.", fuzzer.url()));
                if let Some(runs) = status.runs {
                    self.writer.println(format!("  Runs performed: {}", runs));
                };
                if let Some(elapsed) = status.elapsed {
                    let elapsed: Option<u64> = elapsed.try_into().ok();
                    if let Some(elapsed) = elapsed {
                        let duration = fasync::Duration::from_nanos(elapsed);
                        self.writer
                            .println(format!("    Time elapsed: {} seconds", duration.as_secs()));
                    }
                };
                if let (Some(covered_pcs), Some(covered_features)) =
                    (status.covered_pcs, status.covered_features)
                {
                    self.writer.println(format!(
                        "        Coverage: {} PCs, {} features",
                        covered_pcs, covered_features
                    ));
                };
                if let (Some(corpus_num_inputs), Some(corpus_total_size)) =
                    (status.corpus_num_inputs, status.corpus_total_size)
                {
                    self.writer.println(format!(
                        "     Corpus size: {} inputs, {} total bytes",
                        corpus_num_inputs, corpus_total_size
                    ));
                }
            }
            _ => unreachable!(),
        };
        Ok(())
    }

    // Repeatedly waits for the output to be paused while a long-running workflow is executing.
    async fn handle_pause(&self, fuzzer: &Fuzzer<O>) -> NextAction {
        loop {
            {
                let mut reader = self.reader.borrow_mut();
                if !reader.is_interactive() {
                    return NextAction::Resume;
                }
                if let Err(e) = reader.pause().await {
                    self.writer.error(e);
                    return NextAction::Resume;
                }
            }
            if self.get_state() != FuzzerState::Running {
                return NextAction::Prompt;
            }
            self.writer.pause();
            let next_action = self.while_paused(fuzzer).await;
            if next_action != NextAction::Resume {
                return next_action;
            }
            self.writer.resume();
        }
    }

    // Runs the prompt loop and executes the command while the output of a long-running workflow is
    // paused.
    async fn while_paused(&self, fuzzer: &Fuzzer<O>) -> NextAction {
        self.writer.println("Fuzzer output has been paused.");
        self.writer.println("To resume output, use the `resume` command.");
        loop {
            let result = match self.prompt().await {
                Some(command) => self.execute_running(command, fuzzer).await,
                None => Ok(NextAction::Prompt),
            };
            match result {
                Ok(NextAction::Retry(_)) => {
                    self.writer.error("Invalid command: A long-running workflow is in progress.")
                }
                Ok(NextAction::Prompt) => {
                    // Executed "stop" or "detach".
                    if self.get_state() == FuzzerState::Detached {
                        return NextAction::Prompt;
                    }
                }
                Ok(next_action) => {
                    // Executed "resume" or "exit".
                    return next_action;
                }
                Err(e) => self.writer.error(e),
            };
        }
    }

    fn detach(&self) {
        match self.get_state() {
            FuzzerState::Idle => self.writer.println("Note: fuzzer is idle but still alive."),
            FuzzerState::Running => self.writer.println("Note: fuzzer will continue running."),
            _ => unreachable!(),
        };
        self.writer.println(format!("To reconnect later, use the 'attach' command."));
        self.writer.println(format!("To stop this fuzzer, use 'stop'. command"));
        self.set_state(FuzzerState::Detached);
    }

    async fn stop(&self, url: &str) -> Result<()> {
        let url = Url::parse(url).context("invalid fuzzer URL")?;
        self.writer.println(format!("Stopping '{}'...", url));
        self.set_state(FuzzerState::Detached);
        let manager = Manager::with_remote_control(&self.rc, &self.writer)
            .await
            .context("failed to connect to manager")?;
        let stopped = manager.stop(&url).await.context("manager failed to stop fuzzer")?;
        if stopped {
            self.writer.println("Stopped.");
        } else {
            self.writer.println("Fuzzer is not running.");
        }
        Ok(())
    }

    // Helper functions to make it easier to access and mutate `Arc` and `RefCell` fields, and to
    // limit the scope of the lock guards and borrows.

    fn get_state(&self) -> FuzzerState {
        self.state.lock().unwrap().clone()
    }

    fn set_state(&self, desired: FuzzerState) {
        let mut state_mut = self.state.lock().unwrap();
        *state_mut = desired
    }

    fn get_fuzzer(&self) -> Option<Fuzzer<O>> {
        self.fuzzer.borrow_mut().take()
    }

    fn put_fuzzer(&self, fuzzer: Fuzzer<O>) {
        let mut fuzzer_mut = self.fuzzer.borrow_mut();
        *fuzzer_mut = Some(fuzzer);
    }
}

#[cfg(test)]
mod test_fixtures {
    use {
        super::Shell,
        crate::fuzzer::test_fixtures::FakeFuzzer,
        crate::reader::test_fixtures::ScriptReader,
        crate::test_fixtures::{Test, TEST_URL},
        crate::writer::test_fixtures::BufferSink,
        anyhow::{Context as _, Result},
        ffx_fuzz_args::FuzzerState,
        fidl_fuchsia_fuzzer::Result_ as FuzzResult,
        fuchsia_async as fasync,
        std::fmt::Display,
        std::path::PathBuf,
        url::Url,
    };

    /// Represents a set of test fakes used to test `Shell`.
    pub struct ShellScript {
        shell: Shell<ScriptReader, BufferSink>,
        state: FuzzerState,
        url: Url,
        output_dir: PathBuf,
        runs_indefinitely: bool,
    }

    impl ShellScript {
        /// Creates a shell with fakes suitable for testing.
        pub fn try_new(test: &mut Test) -> Result<Self> {
            let url = Url::parse(TEST_URL).context("failed to parse test URL")?;
            let urls = vec![&url];
            let tests_json = test
                .create_tests_json(urls.iter())
                .context("failed to write URLs for shell script")?;
            let tests_json = Some(tests_json.to_string_lossy().to_string());
            let proxy = test.rcs().context("failed to serve RCS")?;
            let reader = ScriptReader::new();
            let shell = Shell::new(tests_json, proxy, reader, test.writer());
            Ok(Self {
                shell,
                state: FuzzerState::Detached,
                url,
                output_dir: test.root_dir().to_path_buf(),
                runs_indefinitely: false,
            })
        }

        /// Bootstraps the `ShellScript` to emulate having a fuzzer in a long-running workflow.
        pub async fn create_running(test: &mut Test) -> Result<(Self, FakeFuzzer)> {
            let mut script = Self::try_new(test).context("failed to create shell script")?;
            script.runs_indefinitely = true;
            let fuzzer = script.attach(test);
            fuzzer.set_result(Ok(FuzzResult::NoErrors));
            script.add(test, "run");
            test.output_matches("Configuring fuzzer...");
            test.output_matches("Running fuzzer...");
            script.interrupt(test).await;
            Ok((script, fuzzer))
        }

        /// Returns the URL used for fake fuzzers.
        pub fn url(&self) -> &Url {
            &self.url
        }

        /// Adds input commands and output expectations for attaching to a fuzzer.
        pub fn attach(&mut self, test: &mut Test) -> FakeFuzzer {
            let cmdline = format!("attach {} -o {}", self.url, self.output_dir.to_string_lossy());
            self.add(test, cmdline);
            test.output_matches(format!("Attaching to '{}'...", self.url));
            test.output_matches("Attached; fuzzer is idle.");
            self.state = FuzzerState::Idle;
            test.fuzzer()
        }

        /// Adds an input command to run as part of a test.
        pub fn add<S: AsRef<str> + Display>(&mut self, test: &mut Test, command: S) {
            // Handle special cases that don't complete interrupted workflows.
            let cmd = match command.as_ref().split_once(' ') {
                Some((cmd, _)) => cmd,
                None => command.as_ref(),
            };
            match cmd {
                "detach" | "stop" => {
                    self.state = FuzzerState::Detached;
                }
                "try" | "run" | "minimize" | "cleanse" | "merge" => {
                    if self.state == FuzzerState::Idle {
                        test.output_matches("Starting workflow...");
                        test.output_matches("Press any key to pause fuzzer output.");
                        self.state = FuzzerState::Running;
                    }
                }
                _ => {}
            };
            let mut reader = self.shell.reader.borrow_mut();
            reader.add(command);
        }

        /// Processes the previously `add`ed commands using the underlying `Shell`.
        pub async fn run(&mut self, test: &mut Test) -> Result<()> {
            // Handle the case where a workflow expected to fail.
            if test.fuzzer().get_result().is_err() && self.state == FuzzerState::Running {
                self.state = FuzzerState::Detached;
            }

            // If this script is not expected to run indefinitely, but the state still indicates the
            // test is running after all other commands have been added, then this is a long-running
            // workflow that runs to completion.
            if !self.runs_indefinitely && self.state == FuzzerState::Running {
                let mut reader = self.shell.reader.borrow_mut();
                reader.interrupt(fasync::Duration::from_millis(10)).await;
                test.output_matches("Workflow complete. Press any key to continue...");
                self.state = FuzzerState::Idle;
            }

            // The shell automatically exits on EOF.
            test.output_matches("Exiting...");
            self.detach_from_state(test, self.state);

            self.shell.run().await
        }

        /// Simulates the user pressing a key to interrupt output from a long-running workflow.
        pub async fn interrupt(&self, test: &mut Test) {
            let mut reader = self.shell.reader.borrow_mut();
            reader.interrupt(fasync::Duration::from_millis(10)).await;
            test.output_matches("Fuzzer output has been paused.");
            test.output_matches("To resume output, use the `resume` command.");
        }

        /// Simulates detaching from a fuzzer, which may be running or idle.
        pub async fn detach(&mut self, test: &mut Test) {
            let state = self.state;
            self.add(test, "detach");
            test.output_matches(format!("Detaching from '{}'...", self.url));
            self.detach_from_state(test, state);
            test.output_matches("Detached.");
        }

        fn detach_from_state(&mut self, test: &mut Test, state: FuzzerState) {
            self.state = FuzzerState::Detached;
            match state {
                FuzzerState::Idle => test.output_matches("Note: fuzzer is idle but still alive."),
                FuzzerState::Running => test.output_matches("Note: fuzzer will continue running."),
                _ => return,
            };
            test.output_matches("To reconnect later, use the 'attach' command.");
            test.output_matches("To stop this fuzzer, use 'stop'. command");
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::test_fixtures::ShellScript,
        super::DEFAULT_FUZZING_OUTPUT_VARIABLE,
        crate::input::test_fixtures::verify_saved,
        crate::test_fixtures::Test,
        crate::util::digest_path,
        anyhow::Result,
        fidl_fuchsia_fuzzer::{self as fuzz, Result_ as FuzzResult},
        fuchsia_async as fasync, fuchsia_zircon_status as zx,
        std::convert::TryInto,
        std::path::PathBuf,
    };

    #[fuchsia::test]
    async fn test_empty() -> Result<()> {
        let mut test = Test::try_new()?;
        let mut script = ShellScript::try_new(&mut test)?;
        script.add(&mut test, "");
        script.add(&mut test, "   ");
        script.add(&mut test, "# a comment");
        script.add(&mut test, "   # another comment");
        script.run(&mut test).await?;
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_invalid() -> Result<()> {
        let mut test = Test::try_new()?;
        let mut script = ShellScript::try_new(&mut test)?;
        script.add(&mut test, "foobar");
        test.output_includes("Unrecognized argument: foobar");
        test.output_matches("Command is unrecognized or invalid for the current state.");
        test.output_matches("Try 'help' to list recognized.");
        test.output_matches("Try 'status' to check the current state.");
        script.run(&mut test).await?;
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_list() -> Result<()> {
        let mut test = Test::try_new()?;
        let mut script = ShellScript::try_new(&mut test)?;

        // Can 'list' when detached. Try both empty and non-empty lists of URLs.
        let urls: Vec<&str> = vec![];
        test.create_tests_json(urls.iter())?;
        script.add(&mut test, "list");
        test.output_matches("[]");
        script.run(&mut test).await?;
        test.verify_output()?;

        let mut script = ShellScript::try_new(&mut test)?;
        let urls = vec![
            "fuchsia-pkg://fuchsia.com/test-fuzzers#meta/foo-fuzzer.cm",
            "fuchsia-pkg://fuchsia.com/test-fuzzers#meta/bar-fuzzer.cm",
            "fuchsia-pkg://fuchsia.com/test-fuzzers#meta/baz-fuzzer.cm",
        ];
        test.create_tests_json(urls.iter())?;
        script.add(&mut test, "list");
        test.output_matches("[");
        test.output_matches(format!("\"{}\",", urls[0]));
        test.output_matches(format!("\"{}\",", urls[1]));
        test.output_matches(format!("\"{}\"", urls[2]));
        test.output_matches("]");
        script.run(&mut test).await?;
        test.verify_output()?;

        // Can 'list' when idle. Include a pattern.
        script = ShellScript::try_new(&mut test)?;
        test.create_tests_json(urls.iter())?;
        let _fuzzer = script.attach(&mut test);
        script.add(&mut test, "list -p *ba?-fuzzer.cm");
        test.output_matches("[");
        test.output_matches(format!("\"{}\",", urls[1]));
        test.output_matches(format!("\"{}\"", urls[2]));
        test.output_matches("]");
        script.run(&mut test).await?;
        test.verify_output()?;

        // Can 'list' when running.
        let (mut script, _fuzzer) = ShellScript::create_running(&mut test).await?;
        test.create_tests_json(urls.iter())?;
        script.add(&mut test, "list");
        test.output_matches("[");
        test.output_matches(format!("\"{}\",", urls[0]));
        test.output_matches(format!("\"{}\",", urls[1]));
        test.output_matches(format!("\"{}\"", urls[2]));
        test.output_matches("]");
        script.detach(&mut test).await;
        script.run(&mut test).await?;
        test.verify_output()
    }

    // This test is larger than some of the others as it is used to test autocomplation of commands,
    // options, files, and URLs.
    #[fuchsia::test]
    async fn test_attach() -> Result<()> {
        let _env = ffx_config::test_init().await.expect("Unable to initialize ffx_config.");
        let mut test = Test::try_new()?;
        let mut script = ShellScript::try_new(&mut test)?;

        let output_dir = test.create_dir("output")?;
        let test_files = vec!["test1"];
        test.create_test_files(&output_dir, test_files.iter())?;

        // URL must be valid
        script.add(&mut test, "attach invalid-url");
        test.output_includes("invalid fuzzer URL");

        // Output directory must be provided or set in config.
        script.add(&mut test, format!("attach {}", script.url()));
        test.output_includes("output directory is not set");
        script.run(&mut test).await?;
        test.verify_output()?;

        // Output directory from config is checked.
        test = Test::try_new()?;
        script = ShellScript::try_new(&mut test)?;
        let mut badpath = PathBuf::from(test.root_dir());
        badpath.push("invalid");
        ffx_config::query(DEFAULT_FUZZING_OUTPUT_VARIABLE)
            .level(Some(ffx_config::ConfigLevel::User))
            .set(serde_json::json!(&badpath))
            .await?;
        script.add(&mut test, format!("attach {}", script.url()));
        test.output_includes("invalid output directory");

        // Provided output directory is checked.
        script.add(&mut test, format!("attach -o {} {}", badpath.to_string_lossy(), script.url()));
        test.output_includes("invalid output directory");

        // Provided output directory must be a directory.
        let cmdline = format!("attach -o {}/test1 {}", output_dir.to_string_lossy(), script.url());
        script.add(&mut test, cmdline);
        test.output_includes("invalid output directory");

        // Can 'attach' when detached.
        script.attach(&mut test);

        // Cannot 'attach' when attached.
        let cmdline = format!("attach -o {} {}", output_dir.to_string_lossy(), script.url());
        script.add(&mut test, cmdline);
        test.output_includes("invalid command: a fuzzer is already attached.");
        script.run(&mut test).await?;
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_get() -> Result<()> {
        let mut test = Test::try_new()?;
        let mut script = ShellScript::try_new(&mut test)?;

        // Cannot 'get' when detached.
        script.add(&mut test, "get runs");
        test.output_includes("invalid command: no fuzzer attached.");

        // Can 'get' when idle.
        let _fuzzer = script.attach(&mut test);
        script.add(&mut test, "get runs");
        test.output_matches("runs: 0");
        script.run(&mut test).await?;
        test.verify_output()?;

        // Can 'get' when running.
        let (mut script, _fuzzer) = ShellScript::create_running(&mut test).await?;
        script.add(&mut test, "get runs");
        test.output_matches("runs: 0");
        script.detach(&mut test).await;

        script.run(&mut test).await?;
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_set() -> Result<()> {
        let mut test = Test::try_new()?;
        let mut script = ShellScript::try_new(&mut test)?;

        // Cannot 'set' when detached.
        script.add(&mut test, "set runs 10");
        test.output_includes("invalid command: no fuzzer attached.");

        // Can 'set' when idle.
        let _fuzzer = script.attach(&mut test);
        script.add(&mut test, "set runs 10");
        test.output_matches("Configuring fuzzer...");
        test.output_matches("Option 'runs' set to 10");
        script.run(&mut test).await?;
        test.verify_output()?;

        // Cannot 'set' when running.
        let (mut script, _fuzzer) = ShellScript::create_running(&mut test).await?;
        script.add(&mut test, "set runs 20");
        test.output_includes("invalid command: a long-running workflow is in progress.");
        script.detach(&mut test).await;
        script.run(&mut test).await?;
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_add() -> Result<()> {
        let mut test = Test::try_new()?;
        let mut script = ShellScript::try_new(&mut test)?;
        let corpus_dir = test.corpus_dir(fuzz::Corpus::Live);

        // 'add' can take a dir as an argument.
        let test_files = vec!["test1", "test2"];
        test.create_test_files(&corpus_dir, test_files.iter())?;

        // Cannot 'add' when detached.
        script.add(&mut test, format!("add {}", corpus_dir.to_string_lossy()));
        test.output_includes("invalid command: no fuzzer attached.");

        // Can 'add' when idle
        let _fuzzer = script.attach(&mut test);
        script.add(&mut test, format!("add {}", corpus_dir.to_string_lossy()));
        test.output_matches("Adding inputs to fuzzer corpus...");
        test.output_matches("Added 2 inputs totaling 10 bytes to the live corpus.");
        script.run(&mut test).await?;
        test.verify_output()?;

        // Can 'add' when running.
        let (mut script, _fuzzer) = ShellScript::create_running(&mut test).await?;
        script.add(&mut test, format!("add {}", corpus_dir.to_string_lossy()));
        test.output_matches("Adding inputs to fuzzer corpus...");
        test.output_matches("Added 2 inputs totaling 10 bytes to the live corpus.");
        script.detach(&mut test).await;
        script.run(&mut test).await?;
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_try() -> Result<()> {
        let mut test = Test::try_new()?;
        let mut script = ShellScript::try_new(&mut test)?;

        // Cannot 'try' when detached. 'try' can take a hex value as an argument.
        script.add(&mut test, "try deadbeef");
        test.output_includes("invalid command: no fuzzer attached.");

        // Can 'try' when idle.
        let fuzzer = script.attach(&mut test);
        script.add(&mut test, "try deadbeef");
        test.output_matches("Trying an input of 4 bytes...");
        fuzzer.set_result(Ok(FuzzResult::Crash));
        test.output_matches("The input caused a process to crash.");
        script.run(&mut test).await?;
        test.verify_output()?;

        // Errors are propagated correctly.
        let mut script = ShellScript::try_new(&mut test)?;
        let fuzzer = script.attach(&mut test);
        script.add(&mut test, "try deadbeef");
        test.output_matches("Trying an input of 4 bytes...");
        fuzzer.set_result(Err(zx::Status::INTERNAL));
        test.output_includes("failed to execute command: `fuchsia.fuzzer.Controller/Execute` returned: ZX_ERR_INTERNAL");
        script.run(&mut test).await?;
        test.verify_output()?;

        // Cannot 'try' when running.
        let (mut script, _fuzzer) = ShellScript::create_running(&mut test).await?;
        script.add(&mut test, "try feedface");
        test.output_includes("invalid command: a long-running workflow is in progress.");
        script.detach(&mut test).await;
        script.run(&mut test).await?;
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_run() -> Result<()> {
        let mut test = Test::try_new()?;
        let mut script = ShellScript::try_new(&mut test)?;

        // Cannot 'run' when detached. 'run' can take flags as arguments.
        script.add(&mut test, "run -r 20");
        test.output_includes("invalid command: no fuzzer attached.");

        // Can 'run' when idle.
        let fuzzer = script.attach(&mut test);
        script.add(&mut test, "run -r 20");
        test.output_matches("Configuring fuzzer...");
        test.output_matches("Running fuzzer...");
        fuzzer.set_result(Ok(FuzzResult::Death));
        fuzzer.set_input_to_send(b"hello");
        test.output_matches("An input to the fuzzer triggered a sanitizer violation.");
        let artifact = digest_path(test.artifact_dir(), Some("death"), b"hello");
        test.output_matches(format!("Input saved to '{}'", artifact.to_string_lossy()));
        script.run(&mut test).await?;
        let options = fuzzer.get_options();
        assert_eq!(options.runs, Some(20));
        verify_saved(&artifact, b"hello")?;
        test.verify_output()?;

        // Errors are propagated correctly.
        let mut script = ShellScript::try_new(&mut test)?;
        let fuzzer = script.attach(&mut test);
        script.add(&mut test, "run -r 20");
        test.output_matches("Configuring fuzzer...");
        test.output_matches("Running fuzzer...");
        fuzzer.set_result(Err(zx::Status::IO));
        test.output_includes(
            "failed to execute command: `fuchsia.fuzzer.Controller/Fuzz` returned: ZX_ERR_IO",
        );
        script.run(&mut test).await?;
        test.verify_output()?;

        // Cannot 'run' when running.
        let (mut script, _fuzzer) = ShellScript::create_running(&mut test).await?;
        script.add(&mut test, "run -t 20");
        test.output_includes("invalid command: a long-running workflow is in progress.");
        script.detach(&mut test).await;
        script.run(&mut test).await?;
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_cleanse() -> Result<()> {
        let mut test = Test::try_new()?;
        let mut script = ShellScript::try_new(&mut test)?;

        // Cannot 'cleanse' when detached. 'cleanse' can take a hex value as an argument.
        script.add(&mut test, format!("cleanse {}", hex::encode("hello")));
        test.output_includes("invalid command: no fuzzer attached.");

        // Can 'cleanse' when idle.
        let fuzzer = script.attach(&mut test);
        script.add(&mut test, format!("cleanse {}", hex::encode("hello")));
        test.output_matches("Attempting to cleanse an input of 5 bytes...");
        fuzzer.set_input_to_send(b"world");
        let artifact = digest_path(test.artifact_dir(), Some("cleansed"), b"world");
        test.output_matches(format!("Cleansed input written to '{}'", artifact.to_string_lossy()));
        script.run(&mut test).await?;
        verify_saved(&artifact, b"world")?;
        test.verify_output()?;

        // Cannot 'cleanse' when running.
        let (mut script, _fuzzer) = ShellScript::create_running(&mut test).await?;
        script.add(&mut test, format!("cleanse {}", hex::encode("world")));
        test.output_includes("invalid command: a long-running workflow is in progress.");
        script.detach(&mut test).await;
        script.run(&mut test).await?;
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_minimize() -> Result<()> {
        let mut test = Test::try_new()?;
        let mut script = ShellScript::try_new(&mut test)?;

        // Cannot 'minimize' when detached. 'minimize' can take a hex value as an argument.
        script.add(&mut test, format!("minimize {}", hex::encode("hello")));
        test.output_includes("invalid command: no fuzzer attached.");

        // Can 'minimize' when idle.
        let fuzzer = script.attach(&mut test);
        script.add(&mut test, format!("minimize {} -t 10s", hex::encode("hello")));
        test.output_matches("Configuring fuzzer...");
        test.output_matches("Attempting to minimize an input of 5 bytes...");
        fuzzer.set_input_to_send(b"world");
        let artifact = digest_path(test.artifact_dir(), Some("minimized"), b"world");
        test.output_matches(format!("Minimized input written to '{}'", artifact.to_string_lossy()));
        script.run(&mut test).await?;
        verify_saved(&artifact, b"world")?;
        test.verify_output()?;

        // Cannot 'minimize' when running.
        let (mut script, _fuzzer) = ShellScript::create_running(&mut test).await?;
        script.add(&mut test, format!("cleanse {}", hex::encode("world")));
        test.output_includes("invalid command: a long-running workflow is in progress.");
        script.detach(&mut test).await;
        script.run(&mut test).await?;
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_merge() -> Result<()> {
        let mut test = Test::try_new()?;
        let mut script = ShellScript::try_new(&mut test)?;

        // Cannot 'merge' when detached.
        script.add(&mut test, format!("merge"));
        test.output_includes("invalid command: no fuzzer attached.");

        // Can 'merge' when idle.
        let fuzzer = script.attach(&mut test);
        script.add(&mut test, format!("merge"));
        fuzzer.set_input_to_send(b"foo");
        test.output_matches("Compacting fuzzer corpus...");
        test.output_matches("Retrieving fuzzer corpus...");
        test.output_matches("Retrieved 1 input totaling 3 bytes from the live corpus.");
        script.run(&mut test).await?;
        let input = digest_path(test.corpus_dir(fuzz::Corpus::Live), None, b"foo");
        verify_saved(&input, b"foo")?;
        test.verify_output()?;

        // Cannot 'merge' when running.
        let (mut script, _fuzzer) = ShellScript::create_running(&mut test).await?;
        script.add(&mut test, format!("merge"));
        test.output_includes("invalid command: a long-running workflow is in progress.");
        script.detach(&mut test).await;
        script.run(&mut test).await?;
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_resume() -> Result<()> {
        let mut test = Test::try_new()?;
        let mut script = ShellScript::try_new(&mut test)?;

        // Cannot 'resume' when detached.
        script.add(&mut test, "resume");
        test.output_includes("invalid command: no fuzzer attached.");

        // Cannot 'resume' when idle.
        let _fuzzer = script.attach(&mut test);
        script.add(&mut test, "resume");
        test.output_includes("invalid command: no fuzzer running.");
        script.run(&mut test).await?;
        test.verify_output()?;

        // Can 'resume' when running.
        let (mut script, _fuzzer) = ShellScript::create_running(&mut test).await?;
        script.add(&mut test, "resume");
        test.output_matches("Resuming fuzzer output...");
        test.output_matches("Press any key to pause fuzzer output.");
        script.interrupt(&mut test).await;
        script.detach(&mut test).await;
        script.run(&mut test).await?;
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_status() -> Result<()> {
        let mut test = Test::try_new()?;
        let mut script = ShellScript::try_new(&mut test)?;

        // Can get 'status' when detached.
        script.add(&mut test, "status");
        test.output_matches("No fuzzer attached.");

        // Can get 'status' when idle.
        let _fuzzer = script.attach(&mut test);
        script.add(&mut test, "status");
        test.output_matches(format!("{} is idle.", script.url()));
        script.run(&mut test).await?;
        test.verify_output()?;

        // Can get 'status' when running.
        let (mut script, fuzzer) = ShellScript::create_running(&mut test).await?;
        let status = fuzz::Status {
            // running: Some(true),
            runs: Some(1),
            elapsed: Some(fasync::Duration::from_secs(2).as_nanos().try_into().unwrap()),
            covered_pcs: Some(3),
            covered_features: Some(4),
            corpus_num_inputs: Some(5),
            corpus_total_size: Some(6),
            ..fuzz::Status::EMPTY
        };
        fuzzer.set_status(status);
        script.add(&mut test, "status");
        test.output_matches(format!("{} is running.", script.url()));
        test.output_matches("Runs performed: 1");
        test.output_matches("Time elapsed: 2 seconds");
        test.output_matches("Coverage: 3 PCs, 4 features");
        test.output_matches("Corpus size: 5 inputs, 6 total bytes");
        script.detach(&mut test).await;
        script.run(&mut test).await?;
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_fetch() -> Result<()> {
        let mut test = Test::try_new()?;
        let mut script = ShellScript::try_new(&mut test)?;

        // Cannot 'fetch' when detached.
        script.add(&mut test, format!("fetch -s"));
        test.output_includes("invalid command: no fuzzer attached.");

        // Can 'fetch' when idle.
        let fuzzer = script.attach(&mut test);
        script.add(&mut test, format!("fetch -s"));
        fuzzer.set_input_to_send(b"bar");
        test.output_matches("Retrieving fuzzer corpus...");
        test.output_matches("Retrieved 1 input totaling 3 bytes from the seed corpus.");
        script.run(&mut test).await?;
        let input = digest_path(test.corpus_dir(fuzz::Corpus::Seed), None, b"bar");
        verify_saved(&input, b"bar")?;
        test.verify_output()?;

        // Can 'fetch' when running.
        let (mut script, fuzzer) = ShellScript::create_running(&mut test).await?;
        script.add(&mut test, format!("fetch -s"));
        fuzzer.set_input_to_send(b"bar");
        test.output_matches("Retrieving fuzzer corpus...");
        test.output_matches("Retrieved 1 input totaling 3 bytes from the seed corpus.");
        script.detach(&mut test).await;
        script.run(&mut test).await?;
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_detach() -> Result<()> {
        let mut test = Test::try_new()?;
        let mut script = ShellScript::try_new(&mut test)?;

        // Cannot 'detach' when detached.
        script.add(&mut test, "detach");
        test.output_includes("invalid command: no fuzzer attached.");

        // Can 'detach' when idle.
        script.attach(&mut test);
        script.detach(&mut test).await;
        script.run(&mut test).await?;
        test.verify_output()?;

        // Can 'detach' when running.
        let (mut script, _fuzzer) = ShellScript::create_running(&mut test).await?;
        script.detach(&mut test).await;
        script.run(&mut test).await?;
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_stop() -> Result<()> {
        let mut test = Test::try_new()?;
        let mut script = ShellScript::try_new(&mut test)?;

        // Cannot 'stop' without a URL when detached.
        script.add(&mut test, "stop");
        test.output_includes("invalid command: no fuzzer attached.");

        // Trying to 'stop'  a stopped fuzzer using its URL doesn't do anything.
        script.add(&mut test, format!("stop {}", script.url()));
        test.output_matches(format!("Stopping '{}'...", script.url()));
        test.output_matches("Fuzzer is not running.");

        // Can 'stop' a detached fuzzer using its URL.
        script.attach(&mut test);
        script.detach(&mut test).await;
        script.add(&mut test, format!("stop {}", script.url()));
        test.output_matches(format!("Stopping '{}'...", script.url()));
        test.output_matches("Stopped.");

        // Can 'stop' when idle.
        script.attach(&mut test);
        script.add(&mut test, "stop");
        test.output_matches(format!("Stopping '{}'...", script.url()));
        test.output_matches("Stopped.");
        script.run(&mut test).await?;
        test.verify_output()?;

        // Can 'stop' when running.
        let (mut script, _fuzzer) = ShellScript::create_running(&mut test).await?;
        script.add(&mut test, "stop");
        test.output_matches(format!("Stopping '{}'...", script.url()));
        test.output_matches("Stopped.");
        script.run(&mut test).await?;
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_exit() -> Result<()> {
        let mut test = Test::try_new()?;
        let mut script = ShellScript::try_new(&mut test)?;

        // Can 'exit' when detached. The "Exiting..." messages are expected automatically by
        // `script::run`.
        script.add(&mut test, "exit");
        script.add(&mut test, "not executed");
        script.run(&mut test).await?;
        test.verify_output()?;

        // Can 'exit' when idle.
        script = ShellScript::try_new(&mut test)?;
        script.attach(&mut test);
        script.add(&mut test, "exit");
        script.add(&mut test, "not executed");
        script.run(&mut test).await?;
        test.verify_output()?;

        // Can 'exit' when running.
        let (mut script, _fuzzer) = ShellScript::create_running(&mut test).await?;
        script.add(&mut test, "exit");
        script.add(&mut test, "not executed");
        script.run(&mut test).await?;
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_clear() -> Result<()> {
        let mut test = Test::try_new()?;
        let mut script = ShellScript::try_new(&mut test)?;

        // Just make sure this doesn't crash.
        script.add(&mut test, "clear");

        script.run(&mut test).await?;
        Ok(())
    }

    #[fuchsia::test]
    async fn test_history() -> Result<()> {
        let mut test = Test::try_new()?;
        let mut script = ShellScript::try_new(&mut test)?;

        // Just make sure this doesn't crash.
        script.add(&mut test, "history");

        script.run(&mut test).await?;
        Ok(())
    }
}
