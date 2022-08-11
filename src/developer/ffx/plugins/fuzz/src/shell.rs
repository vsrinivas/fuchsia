// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::corpus::get_type as get_corpus_type,
    crate::fuzzer::Fuzzer,
    crate::manager::Manager,
    crate::reader::{ParsedCommand, Reader},
    crate::util::{get_fuzzer_urls, to_out_dir},
    crate::writer::{OutputSink, Writer},
    anyhow::{Context as _, Result},
    ffx_fuzz_args::*,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_fuzzer as fuzz, fuchsia_async as fasync,
    futures::{future, pin_mut, select, Future, FutureExt},
    std::cell::RefCell,
    std::convert::TryInto,
    std::path::{Path, PathBuf},
    std::pin::Pin,
    std::sync::{Arc, Mutex},
    termion::{self, clear, cursor},
    url::Url,
};

/// Interactive fuzzing shell.
pub struct Shell<R: Reader, O: OutputSink> {
    fuchsia_dir: PathBuf,
    manager: Manager<O>,
    state: Arc<Mutex<FuzzerState>>,
    fuzzer: RefCell<Option<Fuzzer<O>>>,
    reader: RefCell<R>,
    writer: Writer<O>,
}

/// Indicates what the shell should do after trying to execute a command.
#[derive(Debug, PartialEq)]
pub enum NextAction {
    // An `execute_*` subroutine did not handle the command, and the next candidate should be tried.
    Retry(FuzzShellCommand),

    // The command was handled, and the user should be prompted for the next command.
    Prompt,

    // The command was handled as a user request to exit the shell.
    Exit,
}

impl<R: Reader, O: OutputSink> Shell<R, O> {
    /// Creates a shell that executes a sequence of fuzzer-related commands.
    ///
    /// The shell may be interactive or scripted, depending on its `reader`. It will produce output
    /// using its `writer`. The fuzzers available for the shell to interact with are discovered
    /// by examining the `fuchsia_dir`.
    pub fn create<P: AsRef<Path>>(
        fuchsia_dir: P,
        mut reader: R,
        writer: &Writer<O>,
    ) -> Result<(Self, ServerEnd<fuzz::ManagerMarker>)> {
        let (proxy, server_end) = fidl::endpoints::create_proxy::<fuzz::ManagerMarker>()
            .context("failed to create proxy for fuchsia.fuzzer.Manager")?;
        let manager = Manager::new(proxy, writer);
        let state = Arc::new(Mutex::new(FuzzerState::Detached));
        reader.start(fuchsia_dir.as_ref(), Arc::clone(&state));
        let shell = Self {
            fuchsia_dir: PathBuf::from(fuchsia_dir.as_ref()),
            manager,
            state,
            fuzzer: RefCell::new(None),
            reader: RefCell::new(reader),
            writer: writer.clone(),
        };
        Ok((shell, server_end))
    }

    /// Runs a blocking loop executing commands from the `reader`.
    pub async fn run(&mut self) {
        loop {
            if let Some(command) = self.prompt().await {
                match self.execute(command).await {
                    Ok(NextAction::Prompt) => {}
                    Ok(NextAction::Exit) => return,
                    Err(e) => self.writer.error(format!("Failed to execute command: {:?}", e)),
                    _ => unreachable!(),
                };
            };
        }
    }

    async fn prompt(&self) -> Option<FuzzShellCommand> {
        let parsed = {
            let mut reader = self.reader.borrow_mut();
            reader.prompt().await
        };
        match parsed {
            Some(ParsedCommand::Empty) => None,
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
                unreachable!("Invalid state: ({:?}, {:?}) for {:?}", state, fuzzer, args)
            }
        }
        .map_err(|e| {
            self.set_state(FuzzerState::Detached);
            e
        })?;
        // Replace the fuzzer unless the shell detached. Execution may have changed state.
        if let Some(fuzzer) = fuzzer {
            match self.get_state() {
                FuzzerState::Detached => {}
                FuzzerState::Idle | FuzzerState::Running => self.put_fuzzer(fuzzer),
            };
        }
        Ok(next_action)
    }

    /// Handles commands that can be run in any state.
    /// May return `NextAction::Retry(_)`.
    async fn execute_any(&self, args: FuzzShellCommand) -> Result<NextAction> {
        match args.command {
            FuzzShellSubcommand::List(ListSubcommand { pattern }) => {
                let mut fuzzer_urls =
                    get_fuzzer_urls(&self.fuchsia_dir).context("failed to get URLs to list")?;
                if let Some(pattern) = pattern {
                    let globbed = glob::Pattern::new(&pattern)
                        .context("failed to create glob from pattern")?;
                    fuzzer_urls.retain(|url| globbed.matches(url.as_str()));
                }
                if fuzzer_urls.is_empty() {
                    self.writer.println("No fuzzers available.");
                } else {
                    self.writer.println("Available fuzzers:");
                    for fuzzer_url in fuzzer_urls {
                        self.writer.println(format!("  {}", fuzzer_url));
                    }
                }
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
                self.attach(&url, output).await.context("failed to attach fuzzer")?;
            }
            FuzzShellSubcommand::Status(StatusShellSubcommand {}) => {
                self.writer.println("No fuzzer attached.");
            }
            FuzzShellSubcommand::Exit(ExitShellSubcommand {}) => {
                self.writer.println("Exiting...");
                return Ok(NextAction::Exit);
            }
            _ => self.writer.error("Invalid command: No fuzzer attached."),
        };
        Ok(NextAction::Prompt)
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
                self.writer.error("Invalid command: A fuzzer is already attached.");
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
            FuzzShellSubcommand::Fetch(FetchShellSubcommand { corpus, seed }) => {
                let corpus = to_out_dir(corpus).context("failed to fetch to corpus directory")?;
                fuzzer
                    .fetch(corpus, get_corpus_type(seed))
                    .await
                    .context("failed to fetch corpus from fuzzer")?;
            }
            FuzzShellSubcommand::Stop(StopShellSubcommand {}) => {
                self.set_state(FuzzerState::Detached);
                self.writer.println(format!("Stopping '{}'...", fuzzer.url()));
                self.manager.stop(&fuzzer.url()).await.context("failed to stop fuzzer")?;
                self.writer.println(format!("Stopped."));
            }
            FuzzShellSubcommand::Detach(DetachShellSubcommand {}) => {
                self.set_state(FuzzerState::Detached);
                self.writer.println(format!("Detached from '{}'.", fuzzer.url()));
                self.writer.println("Note: fuzzer is still running!");
                self.writer.println(format!("To reconnect later, use 'attach {}'", fuzzer.url()));
            }
            FuzzShellSubcommand::Exit(ExitShellSubcommand {}) => {
                self.set_state(FuzzerState::Detached);
                self.writer.println("Exiting...");
                self.writer.println("Note: fuzzer is still running!");
                self.writer.println(format!("To reconnect later, use 'attach {}'", fuzzer.url()));
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
        let workflow_fut: Pin<Box<dyn Future<Output = Result<()>>>> = match args.command {
            FuzzShellSubcommand::Set(SetShellSubcommand { name, value }) => {
                fuzzer.set(&name, &value).await.context("failed to set option on fuzzer")?;
                return Ok(NextAction::Prompt);
            }
            FuzzShellSubcommand::Try(TryShellSubcommand { input }) => {
                Box::pin(fuzzer.try_one(input))
            }
            FuzzShellSubcommand::Run(RunShellSubcommand { runs, time }) => {
                Box::pin(fuzzer.run(runs, time))
            }
            FuzzShellSubcommand::Cleanse(CleanseShellSubcommand { input }) => {
                Box::pin(fuzzer.cleanse(input))
            }
            FuzzShellSubcommand::Minimize(MinimizeShellSubcommand { input, runs, time }) => {
                Box::pin(fuzzer.minimize(input, runs, time))
            }
            FuzzShellSubcommand::Merge(MergeShellSubcommand { corpus }) => {
                let corpus = to_out_dir(corpus).context("failed to merge to corpus directory")?;
                Box::pin(fuzzer.merge(corpus))
            }
            _ => unreachable!(),
        };
        let workflow_fut = workflow_fut.fuse();
        pin_mut!(workflow_fut);

        let mut next_action = NextAction::Prompt;
        self.set_state(FuzzerState::Running);
        loop {
            self.writer.println("Press <ENTER> to interrupt output from fuzzer.");
            let interrupt_fut = self.until_interrupt().fuse();
            pin_mut!(interrupt_fut);
            select! {
                _ = interrupt_fut => {
                    next_action = self.on_interrupt(fuzzer).await;
                }
                result = workflow_fut => {
                    result?;
                    self.set_state(FuzzerState::Idle);
                    // Ugh. The `rustyline` editor is sitting in a blocking, uncancellable read of
                    // stdin at this point. The simplest solution seems to be just to have the user
                    // press <ENTER> again.
                    self.writer.println("Workflow complete! Press <ENTER> to return to prompt.");
                }
            };
            if next_action != NextAction::Prompt || self.get_state() != FuzzerState::Running {
                break;
            }
            self.writer.println("Resuming fuzzer output...");
        }
        Ok(next_action)
    }

    /// Handles commands that can only be run when a fuzzer is running.
    /// Never returns `NextAction::Retry(_)`.
    async fn execute_running(
        &self,
        args: FuzzShellCommand,
        fuzzer: &Fuzzer<O>,
    ) -> Result<NextAction> {
        match self.execute_attached(args, &fuzzer).await {
            Ok(NextAction::Retry(_)) => {
                self.writer.error("Invalid command: A long-running workflow is in progress.");
            }
            other => return other,
        };
        Ok(NextAction::Prompt)
    }

    // Subroutines used by the execution routines above.

    // Connects to a fuzzer given by the `url`.
    async fn attach(&self, url: &str, output: Option<String>) -> Result<()> {
        let url = Url::parse(url).context("failed to attach: invalid fuzzer URL")?;

        let output = to_out_dir(output).context("failed to attach: invalid output directory")?;

        let artifacts = match output.as_ref() {
            Some(output) => Ok(output.clone()),
            None => std::env::current_dir(),
        }
        .context("cannot write to current directory")?;

        self.writer.println(format!("Attaching to '{}'...", url));
        let fuzzer = self
            .manager
            .connect(url, artifacts, output)
            .await
            .context("failed to connect to manager")?;
        let status = fuzzer.status().await.context("failed to get status from fuzzer")?;
        self.put_fuzzer(fuzzer);
        match status.running {
            Some(true) => self.set_state(FuzzerState::Running),
            _ => self.set_state(FuzzerState::Idle),
        };
        self.writer.println("Attached.");
        Ok(())
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

    // Returns a future that does not complete until an interrupt is received, i.e. the user types
    // <ENTER>.
    async fn until_interrupt(&self) {
        loop {
            let one = {
                let mut reader = self.reader.borrow_mut();
                reader.until_enter().await
            };
            match one {
                Ok(_) => break,
                Err(e) => {
                    self.writer.error(e);
                    let fut = future::pending::<()>();
                    let _ = fut.await;
                }
            };
        }
    }

    // Runs the prompt loop once and executes the command before resuming output from a concurrent,
    // long-running workflow.
    async fn on_interrupt(&self, fuzzer: &Fuzzer<O>) -> NextAction {
        self.writer.println("Fuzzer output has been interrupted!");
        self.writer.println("Output will resume after the next command completes.");
        self.writer.pause();
        let result = match self.prompt().await {
            Some(command) => self.execute_running(command, fuzzer).await,
            None => Ok(NextAction::Prompt),
        };
        match result {
            Ok(NextAction::Retry(_)) => {
                self.writer.error("Invalid command: A long-running workflow is in progress.")
            }
            Ok(next_action) => return next_action,
            Err(e) => self.writer.error(e),
        };
        self.writer.resume();
        NextAction::Prompt
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
        crate::manager::test_fixtures::FakeManager,
        crate::reader::test_fixtures::ScriptReader,
        crate::util::test_fixtures::{Test, TEST_URL},
        crate::writer::test_fixtures::BufferSink,
        anyhow::{Context as _, Result},
        ffx_fuzz_args::FuzzerState,
        fidl_fuchsia_fuzzer::Result_ as FuzzResult,
        fuchsia_async as fasync,
        std::fmt::Display,
        std::path::{Path, PathBuf},
        url::Url,
    };

    /// Represents a set of test fakes used to test `Shell`.
    pub struct ShellScript {
        shell: Shell<ScriptReader, BufferSink>,
        state: FuzzerState,
        manager: FakeManager,
        url: Url,
        artifact_dir: PathBuf,
    }

    impl ShellScript {
        /// Creates a shell with fakes suitable for testing.
        pub fn try_new(test: &Test) -> Result<Self> {
            let fuchsia_dir = test.root_dir();
            let reader = ScriptReader::new();
            let (shell, server_end) = Shell::create(fuchsia_dir, reader, test.writer())
                .context("failed to create shell")?;

            let url = Url::parse(TEST_URL).context("failed to parse test URL")?;
            let urls = vec![&url];
            test.create_tests_json(urls.iter()).context("failed to write URLs for shell script")?;

            let artifact_dir = test
                .create_dir("artifacts")
                .context("failed to create 'artifacts' directory for shell script")?;

            Ok(Self {
                shell,
                state: FuzzerState::Detached,
                manager: FakeManager::new(server_end, test),
                url,
                artifact_dir,
            })
        }

        /// Bootstraps the `ShellScript` to emulate having a fuzzer in a long-running workflow.
        pub async fn create_running(test: &mut Test) -> Result<(Self, FakeFuzzer)> {
            let mut script = Self::try_new(test).context("failed to create shell script")?;
            let fuzzer = script.attach(test);
            fuzzer.set_result(FuzzResult::NoErrors);
            script.add_workflow(test, "run");
            test.output_matches("Configuring fuzzer...");
            test.output_matches("Running fuzzer...");
            script.interrupt(test).await;
            Ok((script, fuzzer))
        }

        pub fn url(&self) -> &Url {
            &self.url
        }

        pub fn artifact_dir(&self) -> &Path {
            self.artifact_dir.as_path()
        }

        /// Adds input commands and output expectations for attaching to a fuzzer.
        pub fn attach(&mut self, test: &mut Test) -> FakeFuzzer {
            let cmdline = format!("attach {} -o {}", self.url, self.artifact_dir.to_string_lossy());
            self.add(cmdline);
            test.output_matches(format!("Attaching to '{}'...", self.url));
            test.output_matches("Attached.");
            self.state = FuzzerState::Idle;
            self.manager.clone_fuzzer()
        }

        /// Adds an input command to run as part of a test.
        pub fn add<S: AsRef<str> + Display>(&mut self, command: S) {
            // Handle special cases that don't complete interrupted workflows.
            if command.to_string() == "detach" || command.to_string() == "stop" {
                self.state = FuzzerState::Detached;
            }
            if command.to_string() == "exit" && self.state == FuzzerState::Running {
                self.state = FuzzerState::Idle;
            }
            let mut reader = self.shell.reader.borrow_mut();
            reader.add(command);
        }

        /// Adds an input command for a long-running workflow to run as part of a test.
        pub fn add_workflow<S: AsRef<str> + Display>(&mut self, test: &mut Test, command: S) {
            self.add(command);
            test.output_matches("Press <ENTER> to interrupt output from fuzzer.");
            self.state = FuzzerState::Running;
        }

        /// Processes the previously `add`ed commands using the underlying `Shell`.
        pub async fn run(&mut self, test: &mut Test) {
            // Check if this test uses an interrupted workflow.
            if self.state == FuzzerState::Running {
                test.output_matches("Workflow complete! Press <ENTER> to return to prompt.");
            }

            // The shell automatically exits on EOF.
            test.output_matches("Exiting...");
            if self.state != FuzzerState::Detached {
                test.output_matches("Note: fuzzer is still running!");
                test.output_matches(format!("To reconnect later, use 'attach {}'", self.url));
            }

            self.shell.run().await;
        }

        /// Simulates completing a interruption and running to completion.
        pub async fn resume_and_detach(&mut self, test: &mut Test) {
            test.output_matches("Resuming fuzzer output...");
            test.output_matches("Press <ENTER> to interrupt output from fuzzer.");
            self.interrupt(test).await;
            self.add("detach");
            self.state = FuzzerState::Detached;
            test.output_matches(format!("Detached from '{}'.", self.url));
            test.output_matches("Note: fuzzer is still running!");
            test.output_matches(format!("To reconnect later, use 'attach {}'", self.url));
        }

        async fn interrupt(&mut self, test: &mut Test) {
            let mut reader = self.shell.reader.borrow_mut();
            reader.interrupt(fasync::Duration::from_millis(10)).await;
            test.output_matches("Fuzzer output has been interrupted!");
            test.output_matches("Output will resume after the next command completes.");
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::test_fixtures::ShellScript,
        crate::input::test_fixtures::verify_saved,
        crate::util::digest_path,
        crate::util::test_fixtures::Test,
        anyhow::Result,
        fidl_fuchsia_fuzzer::{self as fuzz, Result_ as FuzzResult},
        fuchsia_async as fasync,
        std::convert::TryInto,
    };

    #[fuchsia::test]
    async fn test_empty() -> Result<()> {
        let mut test = Test::try_new()?;
        let mut script = ShellScript::try_new(&test)?;
        script.add("");
        script.add("   ");
        script.add("# a comment");
        script.add("   # another comment");
        script.run(&mut test).await;
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_invalid() -> Result<()> {
        let mut test = Test::try_new()?;
        let mut script = ShellScript::try_new(&test)?;
        script.add("foobar");
        test.output_includes("Unrecognized argument: foobar");
        test.output_matches("Command is unrecognized or invalid for the current state.");
        test.output_matches("Try 'help' to list recognized.");
        test.output_matches("Try 'status' to check the current state.");
        script.run(&mut test).await;
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_list() -> Result<()> {
        let mut test = Test::try_new()?;
        let mut script = ShellScript::try_new(&test)?;

        // Can 'list' when detached. Try both empty and non-empty lists of URLs.
        let mut urls = vec![];
        test.create_tests_json(urls.iter())?;
        script.add("list");
        test.output_matches("No fuzzers available.");
        script.run(&mut test).await;
        test.verify_output()?;

        script = ShellScript::try_new(&test)?;
        urls = vec![
            "fuchsia-pkg://fuchsia.com/test-fuzzers#meta/foo-fuzzer.cm",
            "fuchsia-pkg://fuchsia.com/test-fuzzers#meta/bar-fuzzer.cm",
            "fuchsia-pkg://fuchsia.com/test-fuzzers#meta/baz-fuzzer.cm",
        ];
        test.create_tests_json(urls.iter())?;
        script.add("list");
        test.output_matches("Available fuzzers:");
        test.output_matches(urls[0]);
        test.output_matches(urls[1]);
        test.output_matches(urls[2]);
        script.run(&mut test).await;
        test.verify_output()?;

        // Can 'list' when idle. Include a pattern.
        script = ShellScript::try_new(&test)?;
        test.create_tests_json(urls.iter())?;
        let _fuzzer = script.attach(&mut test);
        script.add("list -p *ba?-fuzzer.cm");
        test.output_matches("Available fuzzers:");
        test.output_matches(urls[1]);
        test.output_matches(urls[2]);
        script.run(&mut test).await;
        test.verify_output()?;

        // Can 'list' when running.
        let (mut script, _fuzzer) = ShellScript::create_running(&mut test).await?;
        test.create_tests_json(urls.iter())?;
        script.add("list");
        test.output_matches("Available fuzzers:");
        test.output_matches(urls[0]);
        test.output_matches(urls[1]);
        test.output_matches(urls[2]);
        script.resume_and_detach(&mut test).await;
        script.run(&mut test).await;
        test.verify_output()
    }

    // This test is larger than some of the others as it is used to test autocomplation of commands,
    // options, files, and URLs.
    #[fuchsia::test]
    async fn test_attach() -> Result<()> {
        let mut test = Test::try_new()?;
        let mut script = ShellScript::try_new(&test)?;

        let output_dir = test.create_dir("output")?;
        let test_files = vec!["test1"];
        test.create_test_files(&output_dir, test_files.iter())?;

        // Parameters are checked on attaching.
        script.add("attach invalid-url");
        test.output_includes("failed to attach: invalid fuzzer URL");

        script.add(format!("attach -o invalid {}", script.url()));
        test.output_includes("failed to attach: invalid output directory");

        script.add(format!("attach -o {}/test1 {}", output_dir.to_string_lossy(), script.url()));
        test.output_includes("failed to attach: invalid output directory");

        // Can 'attach' when detached.
        script.attach(&mut test);

        // Cannot 'attach' when attached.
        script.add(format!("attach -o {} {}", output_dir.to_string_lossy(), script.url()));
        test.output_includes("Invalid command: A fuzzer is already attached.");
        script.run(&mut test).await;
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_get() -> Result<()> {
        let mut test = Test::try_new()?;
        let mut script = ShellScript::try_new(&test)?;

        // Cannot 'get' when detached.
        script.add("get runs");
        test.output_includes("Invalid command: No fuzzer attached.");

        // Can 'get' when idle.
        let _fuzzer = script.attach(&mut test);
        script.add("get runs");
        test.output_matches("runs: 0");
        script.run(&mut test).await;
        test.verify_output()?;

        // Can 'get' when running.
        let (mut script, _fuzzer) = ShellScript::create_running(&mut test).await?;
        script.add("get runs");
        test.output_matches("runs: 0");
        script.resume_and_detach(&mut test).await;

        script.run(&mut test).await;
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_set() -> Result<()> {
        let mut test = Test::try_new()?;
        let mut script = ShellScript::try_new(&test)?;

        // Cannot 'set' when detached.
        script.add("set runs 10");
        test.output_includes("Invalid command: No fuzzer attached.");

        // Can 'set' when idle.
        let _fuzzer = script.attach(&mut test);
        script.add("set runs 10");
        test.output_matches("Option 'runs' set to 10");
        script.run(&mut test).await;
        test.verify_output()?;

        // Cannot 'set' when running.
        let (mut script, _fuzzer) = ShellScript::create_running(&mut test).await?;
        script.add("set runs 20");
        test.output_includes("Invalid command: A long-running workflow is in progress.");
        script.resume_and_detach(&mut test).await;
        script.run(&mut test).await;
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_add() -> Result<()> {
        let mut test = Test::try_new()?;
        let mut script = ShellScript::try_new(&test)?;
        let corpus_dir = test.create_dir("corpus")?;

        // 'add' can take a dir as an argument.
        let test_files = vec!["test1", "test2"];
        test.create_test_files(&corpus_dir, test_files.iter())?;

        // Cannot 'add' when detached.
        script.add(format!("add {}", corpus_dir.to_string_lossy()));
        test.output_includes("Invalid command: No fuzzer attached.");

        // Can 'add' when idle
        let _fuzzer = script.attach(&mut test);
        script.add(format!("add {}", corpus_dir.to_string_lossy()));
        test.output_matches("Adding inputs to fuzzer corpus...");
        test.output_matches("Added 2 inputs totaling 10 bytes to the live corpus.");
        script.run(&mut test).await;
        test.verify_output()?;

        // Can 'add' when running.
        let (mut script, _fuzzer) = ShellScript::create_running(&mut test).await?;
        script.add(format!("add {}", corpus_dir.to_string_lossy()));
        test.output_matches("Adding inputs to fuzzer corpus...");
        test.output_matches("Added 2 inputs totaling 10 bytes to the live corpus.");
        script.resume_and_detach(&mut test).await;
        script.run(&mut test).await;
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_try() -> Result<()> {
        let mut test = Test::try_new()?;
        let mut script = ShellScript::try_new(&test)?;

        // Cannot 'try' when detached. 'try' can take a hex value as an argument.
        script.add("try deadbeef");
        test.output_includes("Invalid command: No fuzzer attached.");

        // Can 'try' when idle.
        let fuzzer = script.attach(&mut test);
        script.add_workflow(&mut test, "try deadbeef");
        test.output_matches("Trying an input of 4 bytes...");
        fuzzer.set_result(FuzzResult::Crash);
        test.output_matches("The input caused a process to crash.");
        script.run(&mut test).await;
        test.verify_output()?;

        // Cannot 'try' when running.
        let (mut script, _fuzzer) = ShellScript::create_running(&mut test).await?;
        script.add("try feedface");
        test.output_includes("Invalid command: A long-running workflow is in progress.");
        script.resume_and_detach(&mut test).await;
        script.run(&mut test).await;
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_run() -> Result<()> {
        let mut test = Test::try_new()?;
        let mut script = ShellScript::try_new(&test)?;

        // Cannot 'run' when detached. 'run' can take flags as arguments.
        script.add("run -r 20");
        test.output_includes("Invalid command: No fuzzer attached.");

        // Can 'run' when idle.
        let fuzzer = script.attach(&mut test);
        script.add_workflow(&mut test, "run -r 20");
        test.output_matches("Configuring fuzzer...");
        test.output_matches("Running fuzzer...");
        fuzzer.set_result(FuzzResult::Death);
        fuzzer.set_input_to_send(b"hello");
        test.output_matches("An input to the fuzzer triggered a sanitizer violation.");
        let artifact = digest_path(script.artifact_dir(), Some("death"), b"hello");
        test.output_matches(format!("Input saved to '{}'", artifact.to_string_lossy()));
        script.run(&mut test).await;
        let options = fuzzer.get_options();
        assert_eq!(options.runs, Some(20));
        verify_saved(&artifact, b"hello")?;
        test.verify_output()?;

        // Cannot 'run' when running.
        let (mut script, _fuzzer) = ShellScript::create_running(&mut test).await?;
        script.add("run -t 20");
        test.output_includes("Invalid command: A long-running workflow is in progress.");
        script.resume_and_detach(&mut test).await;
        script.run(&mut test).await;
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_cleanse() -> Result<()> {
        let mut test = Test::try_new()?;
        let mut script = ShellScript::try_new(&test)?;

        // Cannot 'cleanse' when detached. 'cleanse' can take a hex value as an argument.
        script.add(format!("cleanse {}", hex::encode("hello")));
        test.output_includes("Invalid command: No fuzzer attached.");

        // Can 'cleanse' when idle.
        let fuzzer = script.attach(&mut test);
        script.add_workflow(&mut test, format!("cleanse {}", hex::encode("hello")));
        test.output_matches("Attempting to cleanse an input of 5 bytes...");
        fuzzer.set_input_to_send(b"world");
        let artifact = digest_path(script.artifact_dir(), Some("cleansed"), b"world");
        test.output_matches(format!("Cleansed input written to '{}'", artifact.to_string_lossy()));
        script.run(&mut test).await;
        verify_saved(&artifact, b"world")?;
        test.verify_output()?;

        // Cannot 'cleanse' when running.
        let (mut script, _fuzzer) = ShellScript::create_running(&mut test).await?;
        script.add(format!("cleanse {}", hex::encode("world")));
        test.output_includes("Invalid command: A long-running workflow is in progress.");
        script.resume_and_detach(&mut test).await;
        script.run(&mut test).await;
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_minimize() -> Result<()> {
        let mut test = Test::try_new()?;
        let mut script = ShellScript::try_new(&test)?;

        // Cannot 'minimize' when detached. 'minimize' can take a hex value as an argument.
        script.add(format!("minimize {}", hex::encode("hello")));
        test.output_includes("Invalid command: No fuzzer attached.");

        // Can 'minimize' when idle.
        let fuzzer = script.attach(&mut test);
        script.add_workflow(&mut test, format!("minimize {} -t 10s", hex::encode("hello")));
        test.output_matches("Configuring fuzzer...");
        test.output_matches("Attempting to minimize an input of 5 bytes...");
        fuzzer.set_input_to_send(b"world");
        let artifact = digest_path(script.artifact_dir(), Some("minimized"), b"world");
        test.output_matches(format!("Minimized input written to '{}'", artifact.to_string_lossy()));
        script.run(&mut test).await;
        verify_saved(&artifact, b"world")?;
        test.verify_output()?;

        // Cannot 'minimize' when running.
        let (mut script, _fuzzer) = ShellScript::create_running(&mut test).await?;
        script.add(format!("cleanse {}", hex::encode("world")));
        test.output_includes("Invalid command: A long-running workflow is in progress.");
        script.resume_and_detach(&mut test).await;
        script.run(&mut test).await;
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_merge() -> Result<()> {
        let mut test = Test::try_new()?;
        let mut script = ShellScript::try_new(&test)?;
        let corpus_dir = test.create_dir("corpus")?;

        // Cannot 'merge' when detached.
        script.add(format!("merge -c {}", corpus_dir.to_string_lossy()));
        test.output_includes("Invalid command: No fuzzer attached.");

        // Can 'merge' when idle.
        let fuzzer = script.attach(&mut test);
        script.add_workflow(&mut test, format!("merge -c {}", corpus_dir.to_string_lossy()));
        fuzzer.set_input_to_send(b"foo");
        test.output_matches("Compacting fuzzer corpus...");
        test.output_matches("Retrieving fuzzer corpus...");
        test.output_matches("Retrieved 1 input totaling 3 bytes from the live corpus.");
        script.run(&mut test).await;
        let input = digest_path(&corpus_dir, None, b"foo");
        verify_saved(&input, b"foo")?;
        test.verify_output()?;

        // Cannot 'merge' when running.
        let (mut script, _fuzzer) = ShellScript::create_running(&mut test).await?;
        script.add(format!("merge"));
        test.output_includes("Invalid command: A long-running workflow is in progress.");
        script.resume_and_detach(&mut test).await;
        script.run(&mut test).await;
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_status() -> Result<()> {
        let mut test = Test::try_new()?;
        let mut script = ShellScript::try_new(&test)?;

        // Can get 'status' when detached.
        script.add("status");
        test.output_matches("No fuzzer attached.");

        // Can get 'status' when idle.
        let _fuzzer = script.attach(&mut test);
        script.add("status");
        test.output_matches(format!("{} is idle.", script.url()));
        script.run(&mut test).await;
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
        script.add("status");
        test.output_matches(format!("{} is running.", script.url()));
        test.output_matches("Runs performed: 1");
        test.output_matches("Time elapsed: 2 seconds");
        test.output_matches("Coverage: 3 PCs, 4 features");
        test.output_matches("Corpus size: 5 inputs, 6 total bytes");
        script.resume_and_detach(&mut test).await;
        script.run(&mut test).await;
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_fetch() -> Result<()> {
        let mut test = Test::try_new()?;
        let mut script = ShellScript::try_new(&test)?;
        let corpus_dir = test.create_dir("corpus")?;

        // Cannot 'fetch' when detached.
        script.add(format!("fetch -s -c {}", corpus_dir.to_string_lossy()));
        test.output_includes("Invalid command: No fuzzer attached.");

        // Can 'fetch' when idle.
        let fuzzer = script.attach(&mut test);
        script.add(format!("fetch -s -c {}", corpus_dir.to_string_lossy()));
        fuzzer.set_input_to_send(b"bar");
        test.output_matches("Retrieving fuzzer corpus...");
        test.output_matches("Retrieved 1 input totaling 3 bytes from the seed corpus.");
        script.run(&mut test).await;
        let input = digest_path(&corpus_dir, None, b"bar");
        verify_saved(&input, b"bar")?;
        test.verify_output()?;

        // Can 'fetch' when running.
        let (mut script, fuzzer) = ShellScript::create_running(&mut test).await?;
        script.add(format!("fetch -s -c {}", corpus_dir.to_string_lossy()));
        fuzzer.set_input_to_send(b"bar");
        test.output_matches("Retrieving fuzzer corpus...");
        test.output_matches("Retrieved 1 input totaling 3 bytes from the seed corpus.");
        script.resume_and_detach(&mut test).await;
        script.run(&mut test).await;
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_detach() -> Result<()> {
        let mut test = Test::try_new()?;
        let mut script = ShellScript::try_new(&test)?;

        // Cannot 'detach' when detached.
        script.add("detach");
        test.output_includes("Invalid command: No fuzzer attached.");

        // Can 'detach' when idle.
        script.attach(&mut test);
        script.add("detach");
        test.output_matches(format!("Detached from '{}'.", script.url()));
        test.output_matches("Note: fuzzer is still running!");
        test.output_matches(format!("To reconnect later, use 'attach {}'", script.url()));
        script.run(&mut test).await;
        test.verify_output()?;

        // Can 'detach' when running.
        let (mut script, _fuzzer) = ShellScript::create_running(&mut test).await?;
        script.add("detach");
        test.output_matches(format!("Detached from '{}'.", script.url()));
        test.output_matches("Note: fuzzer is still running!");
        test.output_matches(format!("To reconnect later, use 'attach {}'", script.url()));
        script.run(&mut test).await;
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_stop() -> Result<()> {
        let mut test = Test::try_new()?;
        let mut script = ShellScript::try_new(&test)?;

        // Cannot 'detach' when detached.
        script.add("stop");
        test.output_includes("Invalid command: No fuzzer attached.");

        // Can 'stop' when idle.
        script.attach(&mut test);
        script.add("stop");
        test.output_matches(format!("Stopping '{}'...", script.url()));
        test.output_matches("Stopped.");
        script.run(&mut test).await;
        test.verify_output()?;

        // Can 'stop' when running.
        let (mut script, _fuzzer) = ShellScript::create_running(&mut test).await?;
        script.add("stop");
        test.output_matches(format!("Stopping '{}'...", script.url()));
        test.output_matches("Stopped.");
        script.run(&mut test).await;
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_exit() -> Result<()> {
        let mut test = Test::try_new()?;
        let mut script = ShellScript::try_new(&test)?;

        // Can 'exit' when detached. The "Exiting..." messages are expected automatically by
        // `script::run`.
        script.add("exit");
        script.add("not executed");
        script.run(&mut test).await;
        test.verify_output()?;

        // Can 'exit' when idle.
        script = ShellScript::try_new(&test)?;
        script.attach(&mut test);
        script.add("exit");
        script.add("not executed");
        script.run(&mut test).await;
        test.verify_output()?;

        // Can 'exit' when running.
        let (mut script, _fuzzer) = ShellScript::create_running(&mut test).await?;
        script.add("exit");
        script.add("not executed");
        script.run(&mut test).await;
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_clear() -> Result<()> {
        let mut test = Test::try_new()?;
        let mut script = ShellScript::try_new(&test)?;

        // Just make sure this doesn't crash.
        script.add("clear");

        script.run(&mut test).await;
        Ok(())
    }

    #[fuchsia::test]
    async fn test_history() -> Result<()> {
        let mut test = Test::try_new()?;
        let mut script = ShellScript::try_new(&test)?;

        // Just make sure this doesn't crash.
        script.add("history");

        script.run(&mut test).await;
        Ok(())
    }
}
