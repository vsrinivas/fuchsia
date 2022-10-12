// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::artifact::Artifact,
    crate::corpus,
    crate::diagnostics::Forwarder,
    crate::duration::deadline_after,
    crate::input::{Input, InputPair},
    crate::writer::{OutputSink, Writer},
    anyhow::{bail, Context as _, Error, Result},
    fidl::endpoints::create_request_stream,
    fidl_fuchsia_fuzzer::{self as fuzz, Result_ as FuzzResult},
    fuchsia_async::Timer,
    fuchsia_zircon_status as zx,
    futures::future::{pending, Either},
    futures::{pin_mut, select, try_join, Future, FutureExt},
    std::cell::RefCell,
    std::path::Path,
};

/// Represents a `fuchsia.fuzzer.Controller` connection to a fuzzer.
#[derive(Debug)]
pub struct Controller<O: OutputSink> {
    proxy: fuzz::ControllerProxy,
    forwarder: Forwarder<O>,
    timeout: RefCell<Option<i64>>,
}

impl<O: OutputSink> Controller<O> {
    /// Returns a new Controller instance.
    pub fn new(proxy: fuzz::ControllerProxy, writer: &Writer<O>) -> Self {
        Self { proxy, forwarder: Forwarder::<O>::new(writer), timeout: RefCell::new(None) }
    }

    /// Registers the provided output socket with the forwarder.
    pub fn set_output<P: AsRef<Path>>(
        &mut self,
        socket: fidl::Socket,
        output: fuzz::TestOutput,
        logs_dir: &Option<P>,
    ) -> Result<()> {
        self.forwarder.set_output(socket, output, logs_dir)
    }

    /// Sets various execution and error detection parameters for the fuzzer.
    ///
    /// Returns an error if:
    ///   * Communicating with the fuzzer fails
    ///   * A long-running call such as `try_one`, `fuzz`, `cleanse`, `minimize`, or `merge` is in
    ///     progress.
    ///
    pub async fn configure(&self, options: fuzz::Options) -> Result<()> {
        self.set_timeout(&options);
        let status = match self.proxy.configure(options).await {
            Err(e) => bail!("`fuchsia.fuzzer.Controller/Configure` failed: {:?}", e),
            Ok(raw) => zx::Status::from_raw(raw),
        };
        match status {
            zx::Status::OK => Ok(()),
            zx::Status::BAD_STATE => bail!("a long-running workflow is in progress"),
            status => bail!("`fuchsia.fuzzer.Controller/Configure` returned: ZX_ERR_{}", status),
        }
    }

    /// Returns a fuzzer's current values for the various execution and error detection parameters.
    ///
    /// Returns an error if communicating with the fuzzer fails
    ///
    pub async fn get_options(&self) -> Result<fuzz::Options> {
        let options = self
            .proxy
            .get_options()
            .await
            .map_err(Error::msg)
            .context("`fuchsia.fuzzer.Controller/GetOptions` failed")?;
        self.set_timeout(&options);
        Ok(options)
    }

    // Sets a workflow timeout based on the maximum total time a fuzzer workflow is expected to run.
    fn set_timeout(&self, options: &fuzz::Options) {
        if let Some(max_total_time) = options.max_total_time {
            let mut timeout_mut = self.timeout.borrow_mut();
            match max_total_time {
                0 => {
                    *timeout_mut = None;
                }
                n => {
                    *timeout_mut = Some(n * 2);
                }
            }
        }
    }

    /// Retrieves test inputs from one of the fuzzer's corpora.
    ///
    /// The compacted corpus is saved to the `corpus_dir`. Returns details on how much data was
    /// received.
    ///
    /// Returns an error if:
    ///   * Communicating with the fuzzer fails.
    ///   * One or more inputs fails to be received and saved.
    ///
    pub async fn read_corpus<P: AsRef<Path>>(
        &self,
        corpus_type: fuzz::Corpus,
        corpus_dir: P,
    ) -> Result<corpus::Stats> {
        let (client_end, stream) = create_request_stream::<fuzz::CorpusReaderMarker>()
            .context("failed to create fuchsia.fuzzer.CorpusReader stream")?;
        let (_, corpus_stats) = try_join!(
            async { self.proxy.read_corpus(corpus_type, client_end).await.map_err(Error::msg) },
            async { corpus::read(stream, corpus_dir).await },
        )
        .context("`fuchsia.fuzzer.Controller/ReadCorpus` failed")?;
        Ok(corpus_stats)
    }

    /// Adds a test input to one of the fuzzer's corpora.
    ///
    /// The `test_input` may be either a single file or a directory. Returns details on how much
    /// data was sent.
    ///
    /// Returns an error if:
    ///   * Converting the input to an `Input`/`fuchsia.fuzzer.Input` pair fails.
    ///   * Communicating with the fuzzer fails
    ///   * The fuzzer returns an error, e.g. if it failed to transfer the input.
    ///
    pub async fn add_to_corpus(
        &self,
        input_pairs: Vec<InputPair>,
        corpus_type: fuzz::Corpus,
    ) -> Result<corpus::Stats> {
        let mut corpus_stats = corpus::Stats { num_inputs: 0, total_size: 0 };
        for input_pair in input_pairs.into_iter() {
            let (mut fidl_input, input) = input_pair.as_tuple();
            corpus_stats.num_inputs += 1;
            corpus_stats.total_size += fidl_input.size;
            let (raw, _) = try_join!(
                async {
                    self.proxy.add_to_corpus(corpus_type, &mut fidl_input).await.map_err(Error::msg)
                },
                input.send(),
            )
            .context("`fuchsia.fuzzer.Controller/AddToCorpus` failed")?;
            match zx::Status::from_raw(raw) {
                zx::Status::OK => {}
                status => {
                    bail!("`fuchsia.fuzzer.Controller/AddToCorpus` returned: ZX_ERR_{}", status)
                }
            };
        }
        Ok(corpus_stats)
    }

    /// Returns information about fuzzer execution.
    ///
    /// The status typically includes information such as how long the fuzzer has been running, how
    /// many edges in the call graph have been covered, how large the corpus is, etc.
    ///
    /// Refer to `fuchsia.fuzzer.Status` for precise details on the returned information.
    ///
    pub async fn get_status(&self) -> Result<fuzz::Status> {
        match self.proxy.get_status().await {
            Err(fidl::Error::ClientChannelClosed { status, .. })
                if status == zx::Status::PEER_CLOSED =>
            {
                return Ok(fuzz::Status { ..fuzz::Status::EMPTY })
            }
            Err(e) => bail!("`fuchsia.fuzzer.Controller/GetStatus` failed: {:?}", e),
            Ok(fuzz_status) => Ok(fuzz_status),
        }
    }

    /// Executes the fuzzer once using the given input.
    ///
    /// Returns an error if:
    ///   * Converting the input to an `Input`/`fuchsia.fuzzer.Input` pair fails.
    ///   * Communicating with the fuzzer fails.
    ///   * The fuzzer returns an error, e.g. it is already performing another workflow.
    ///
    pub async fn execute(&self, input_pair: InputPair) -> Result<Artifact> {
        let (mut fidl_input, input) = input_pair.as_tuple();
        let fidl_fut = || async move {
            let result = match self.proxy.execute(&mut fidl_input).await {
                Err(fidl::Error::ClientChannelClosed { status, .. })
                    if status == zx::Status::PEER_CLOSED =>
                {
                    return Ok(Artifact::canceled())
                }
                Err(e) => bail!("`fuchsia.fuzzer.Controller/Execute` failed: {:?}", e),
                Ok(result) => result.map_err(|raw| zx::Status::from_raw(raw)),
            };
            match result {
                Err(zx::Status::BAD_STATE) => bail!("another long-running workflow is in progress"),
                Err(status) => {
                    bail!("`fuchsia.fuzzer.Controller/Execute` returned: ZX_ERR_{}", status)
                }
                Ok(result) => Ok(Artifact::from_result(result)),
            }
        };
        self.with_forwarding(fidl_fut(), Some(input)).await
    }

    /// Runs the fuzzer in a loop to generate and test new inputs.
    ///
    /// The fuzzer will continuously generate new inputs and execute them until one of four
    /// conditions are met:
    ///   * The number of inputs tested exceeds the configured number of `runs`.
    ///   * The configured amount of `max_total_time` has elapsed.
    ///   * An input triggers a fatal error, e.g. death by AddressSanitizer.
    ///   * `fuchsia.fuzzer.Controller/Stop` is called.
    ///
    /// Returns an error if:
    ///   * Either `runs` or `time` is provided but cannot be parsed to  a valid value.
    ///   * Communicating with the fuzzer fails.
    ///   * The fuzzer returns an error, e.g. it is already performing another workflow.
    ///   * The error-causing input, or "artifact", fails to be received and saved.
    ///
    pub async fn fuzz<P: AsRef<Path>>(&self, artifact_dir: P) -> Result<Artifact> {
        let fidl_fut = || async move {
            let result = match self.proxy.fuzz().await {
                Err(fidl::Error::ClientChannelClosed { status, .. })
                    if status == zx::Status::PEER_CLOSED =>
                {
                    return Ok(Artifact::canceled());
                }
                Err(e) => bail!("`fuchsia.fuzzer.Controller/Fuzz` failed: {:?}", e),
                Ok(result) => result.map_err(|raw| zx::Status::from_raw(raw)),
            };
            match result {
                Err(zx::Status::BAD_STATE) => bail!("another long-running workflow is in progress"),
                Err(status) => {
                    bail!("`fuchsia.fuzzer.Controller/Fuzz` returned: ZX_ERR_{}", status)
                }
                Ok((FuzzResult::NoErrors, _)) => Ok(Artifact::ok()),
                Ok((result, input)) => Artifact::try_from_input(result, input, artifact_dir).await,
            }
        };
        self.with_forwarding(fidl_fut(), None).await
    }

    /// Replaces bytes in a error-causing input with PII-safe bytes, e.g. spaces.
    ///
    /// The fuzzer will try to reproduce the error caused by the input with each byte replaced by a
    /// fixed number of "clean" candidates.
    ///
    /// Returns an error if:
    ///   * Converting the input to an `Input`/`fuchsia.fuzzer.Input` pair fails.
    ///   * Communicating with the fuzzer fails.
    ///   * The fuzzer returns an error, e.g. it is already performing another workflow.
    ///   * The cleansed input fails to be received and saved.
    ///
    pub async fn cleanse<P: AsRef<Path>>(
        &self,
        input_pair: InputPair,
        artifact_dir: P,
    ) -> Result<Artifact> {
        let (mut fidl_input, input) = input_pair.as_tuple();
        let fidl_fut = || async move {
            let result = match self.proxy.cleanse(&mut fidl_input).await {
                Err(fidl::Error::ClientChannelClosed { status, .. })
                    if status == zx::Status::PEER_CLOSED =>
                {
                    return Ok(Artifact::canceled())
                }
                Err(e) => bail!("`fuchsia.fuzzer.Controller/Cleanse` failed: {:?}", e),
                Ok(result) => result.map_err(|raw| zx::Status::from_raw(raw)),
            };
            match result {
                Err(zx::Status::BAD_STATE) => bail!("another long-running workflow is in progress"),
                Err(zx::Status::INVALID_ARGS) => bail!("the provided input did not cause an error"),
                Err(status) => {
                    bail!("`fuchsia.fuzzer.Controller/Cleanse` returned: ZX_ERR_{}", status)
                }
                Ok(cleansed) => {
                    Artifact::try_from_input(FuzzResult::Cleansed, cleansed, artifact_dir).await
                }
            }
        };
        self.with_forwarding(fidl_fut(), Some(input)).await
    }

    /// Reduces the length of an error-causing input while preserving the error.
    ///
    /// The fuzzer will bound its attempt to find shorter inputs using the given `runs` or `time`,
    /// if provided.
    ///
    /// Returns an error if:
    ///   * Either `runs` or `time` is provided but cannot be parsed to  a valid value.
    ///   * Converting the input to an `Input`/`fuchsia.fuzzer.Input` pair fails.
    ///   * Communicating with the fuzzer fails.
    ///   * The fuzzer returns an error, e.g. it is already performing another workflow.
    ///   * The minimized input fails to be received and saved.
    ///
    pub async fn minimize<P: AsRef<Path>>(
        &self,
        input_pair: InputPair,
        artifact_dir: P,
    ) -> Result<Artifact> {
        let (mut fidl_input, input) = input_pair.as_tuple();
        let fidl_fut = || async move {
            let result = match self.proxy.minimize(&mut fidl_input).await {
                Err(fidl::Error::ClientChannelClosed { status, .. })
                    if status == zx::Status::PEER_CLOSED =>
                {
                    return Ok(Artifact::canceled())
                }
                Err(e) => bail!("`fuchsia.fuzzer.Controller/Minimize` failed: {:?}", e),
                Ok(result) => result.map_err(|raw| zx::Status::from_raw(raw)),
            };
            match result {
                Err(zx::Status::BAD_STATE) => bail!("another long-running workflow is in progress"),
                Err(zx::Status::INVALID_ARGS) => bail!("the provided input did not cause an error"),
                Err(status) => {
                    bail!("`fuchsia.fuzzer.Controller/Minimize` returned: ZX_ERR_{}", status)
                }
                Ok(minimized) => {
                    Artifact::try_from_input(FuzzResult::Minimized, minimized, artifact_dir).await
                }
            }
        };
        self.with_forwarding(fidl_fut(), Some(input)).await
    }

    /// Removes inputs from the corpus that produce duplicate coverage.
    ///
    /// The fuzzer makes a finite number of passes over its seed and live corpora. The seed corpus
    /// is unchanged, but the fuzzer will try to find the set of shortest inputs that preserves
    /// coverage.
    ///
    /// Returns an error if:
    ///   * Communicating with the fuzzer fails.
    ///   * The fuzzer returns an error, e.g. it is already performing another workflow.
    ///   * One or more inputs fails to be received and saved.
    ///
    pub async fn merge(&self) -> Result<Artifact> {
        let fidl_fut = || async move {
            let status = match self.proxy.merge().await {
                Err(fidl::Error::ClientChannelClosed { status, .. })
                    if status == zx::Status::PEER_CLOSED =>
                {
                    return Ok(Artifact::canceled())
                }
                Err(e) => bail!("`fuchsia.fuzzer.Controller/Merge` failed: {:?}", e),
                Ok(raw) => zx::Status::from_raw(raw),
            };
            match status {
                zx::Status::OK => Ok(Artifact::from_result(FuzzResult::Merged)),
                zx::Status::BAD_STATE => bail!("another long-running workflow is in progress"),
                zx::Status::INVALID_ARGS => bail!("an input in the seed corpus triggered an error"),
                status => bail!("`fuchsia.fuzzer.Controller/Merge` returned: ZX_ERR_{}", status),
            }
        };
        self.with_forwarding(fidl_fut(), None).await
    }

    // Runs the given |fidl_fut| along with futures to optionally send an |input| and forward
    // fuzzer output.
    async fn with_forwarding<F>(&self, fidl_fut: F, input: Option<Input>) -> Result<Artifact>
    where
        F: Future<Output = Result<Artifact>>,
    {
        let fidl_fut = fidl_fut.fuse();
        let send_fut = || async move {
            match input {
                Some(input) => input.send().await,
                None => Ok(()),
            }
        };
        let send_fut = send_fut().fuse();
        let forward_fut = self.forwarder.forward_all().fuse();
        let timer_fut = match deadline_after(self.timeout.take()) {
            Some(deadline) => Either::Left(Timer::new(deadline)),
            None => Either::Right(pending()),
        };
        let timer_fut = timer_fut.fuse();
        pin_mut!(fidl_fut, send_fut, forward_fut, timer_fut);
        let mut remaining = 3;
        let mut artifact = Artifact::ok();
        // If `fidl_fut` completes with e.g. `Ok(zx::Status::CANCELED)`, drop
        // the `send_fut` and `forward_fut` futures.
        while remaining > 0 && artifact.status == zx::Status::OK {
            select! {
                result = fidl_fut => {
                    artifact = result?;
                    remaining -= 1;
                }
                result = send_fut => {
                    result?;
                    remaining -= 1;
                }
                result = forward_fut => {
                    result?;
                    remaining -= 1;
                }
                _ = timer_fut => {
                    bail!("workflow timed out");
                }
            };
        }
        Ok(artifact)
    }
}

#[cfg(test)]
pub mod test_fixtures {
    use {
        super::Controller,
        crate::constants::*,
        crate::diagnostics::test_fixtures::send_log_entry,
        crate::input::InputPair,
        crate::test_fixtures::{create_task, Test},
        crate::writer::test_fixtures::BufferSink,
        anyhow::{anyhow, Context as _, Result},
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_fuzzer::{self as fuzz, Input as FidlInput, Result_ as FuzzResult},
        fuchsia_async as fasync, fuchsia_zircon_status as zx,
        futures::{join, AsyncReadExt, AsyncWriteExt, StreamExt},
        std::cell::RefCell,
        std::rc::Rc,
    };

    /// Creates a test setup suitable for unit testing `Controller`.
    ///
    /// On success, returns a tuple of a `FakeController`, a `Controller`, and a `Task`. The task
    /// will serve a `fuchsia.fuzzer.Controller` stream until it is dropped. The controller holds
    /// the other end of this FIDL channel in its `proxy` field. The test fake has various fields
    /// that it shares with the controller task; these can be accessed or mutated by unit tests to
    /// check what was sent via FIDL requests or what should be sent in FIDL responses. Both the
    /// test fake and the controller are configured to write to the test `output`.
    ///
    /// Returns an error if it fails to create or associate any of the objects with each other.
    ///
    pub fn perform_test_setup(
        test: &Test,
    ) -> Result<(FakeController, Controller<BufferSink>, fasync::Task<()>)> {
        let (proxy, stream) = create_proxy_and_stream::<fuzz::ControllerMarker>()
            .context("failed to create FIDL connection")?;
        let fake = FakeController::new();
        let controller = Controller::new(proxy, test.writer());
        let task = create_task(serve_controller(stream, fake.clone()), test.writer());
        Ok((fake, controller, task))
    }

    /// Add defaults values to an `Options` struct.
    pub fn add_defaults(options: &mut fuzz::Options) {
        options.runs = options.runs.or(Some(0));
        options.max_total_time = options.max_total_time.or(Some(0));
        options.seed = options.seed.or(Some(0));
        options.max_input_size = options.max_input_size.or(Some(1 * BYTES_PER_MB));
        options.mutation_depth = options.mutation_depth.or(Some(5));
        options.dictionary_level = options.dictionary_level.or(Some(0));
        options.detect_exits = options.detect_exits.or(Some(false));
        options.detect_leaks = options.detect_leaks.or(Some(false));
        options.run_limit = options.run_limit.or(Some(20 * NANOS_PER_MINUTE));
        options.malloc_limit = options.malloc_limit.or(Some(2 * BYTES_PER_GB));
        options.oom_limit = options.oom_limit.or(Some(2 * BYTES_PER_GB));
        options.purge_interval = options.purge_interval.or(Some(1 * NANOS_PER_SECOND));
        options.malloc_exitcode = options.malloc_exitcode.or(Some(2000));
        options.death_exitcode = options.death_exitcode.or(Some(2001));
        options.leak_exitcode = options.leak_exitcode.or(Some(2002));
        options.oom_exitcode = options.oom_exitcode.or(Some(2003));
        options.pulse_interval = options.pulse_interval.or(Some(20 * NANOS_PER_SECOND));
        options.debug = options.debug.or(Some(false));
        options.print_final_stats = options.print_final_stats.or(Some(false));
        options.use_value_profile = options.use_value_profile.or(Some(false));
        if options.sanitizer_options.is_none() {
            options.sanitizer_options =
                Some(fuzz::SanitizerOptions { name: String::default(), value: String::default() });
        }
    }

    /// Test fake that allows configuring how to respond to `fuchsia.fuzzer.Controller` methods.
    ///
    /// These fields are Rc<RefCell<_>> in order to be cloned and shared with the `Task` serving the
    /// controller. Unit tests can use this object to query values passed in FIDL requests and set
    /// values returned by FIDL responses.
    #[derive(Debug)]
    pub struct FakeController {
        corpus_type: Rc<RefCell<fuzz::Corpus>>,
        input_to_send: Rc<RefCell<Vec<u8>>>,
        options: Rc<RefCell<fuzz::Options>>,
        received_input: Rc<RefCell<Vec<u8>>>,
        result: Rc<RefCell<Result<FuzzResult, zx::Status>>>,
        status: Rc<RefCell<fuzz::Status>>,
        stdout: Rc<RefCell<Option<fasync::Socket>>>,
        stderr: Rc<RefCell<Option<fasync::Socket>>>,
        syslog: Rc<RefCell<Option<fasync::Socket>>>,
        canceled: Rc<RefCell<bool>>,
    }

    impl FakeController {
        /// Creates a fake controller that can serve `fuchsia.fuzzer.Controller`.
        pub fn new() -> Self {
            let status = fuzz::Status { running: Some(false), ..fuzz::Status::EMPTY };
            let mut options = fuzz::Options::EMPTY;
            add_defaults(&mut options);
            Self {
                corpus_type: Rc::new(RefCell::new(fuzz::Corpus::Seed)),
                input_to_send: Rc::new(RefCell::new(Vec::new())),
                options: Rc::new(RefCell::new(options)),
                received_input: Rc::new(RefCell::new(Vec::new())),
                result: Rc::new(RefCell::new(Ok(FuzzResult::NoErrors))),
                status: Rc::new(RefCell::new(status)),
                stdout: Rc::new(RefCell::new(None)),
                stderr: Rc::new(RefCell::new(None)),
                syslog: Rc::new(RefCell::new(None)),
                canceled: Rc::new(RefCell::new(false)),
            }
        }

        /// Simulates a call to `fuchsia.fuzzer.Manager/GetOutput` without a `fuzz-manager`.
        pub fn set_output(&self, output: fuzz::TestOutput, socket: fidl::Socket) -> zx::Status {
            let socket = fasync::Socket::from_socket(socket).expect("failed to create sockets");
            match output {
                fuzz::TestOutput::Stdout => {
                    let mut stdout_mut = self.stdout.borrow_mut();
                    *stdout_mut = Some(socket);
                }
                fuzz::TestOutput::Stderr => {
                    let mut stderr_mut = self.stderr.borrow_mut();
                    *stderr_mut = Some(socket);
                }
                fuzz::TestOutput::Syslog => {
                    let mut syslog_mut = self.syslog.borrow_mut();
                    *syslog_mut = Some(socket);
                }
                _ => todo!("not supported"),
            }
            zx::Status::OK
        }

        /// Returns the type of corpus received via FIDL requests.
        pub fn get_corpus_type(&self) -> fuzz::Corpus {
            self.corpus_type.borrow().clone()
        }

        /// Sets the type of corpus to return via FIDL responses.
        pub fn set_corpus_type(&self, corpus_type: fuzz::Corpus) {
            let mut corpus_type_mut = self.corpus_type.borrow_mut();
            *corpus_type_mut = corpus_type;
        }

        /// Returns the test input to be sent via a FIDL response.
        pub fn get_input_to_send(&self) -> Vec<u8> {
            self.input_to_send.borrow().clone()
        }

        /// Sets the test input to be sent via a FIDL response.
        pub fn set_input_to_send(&self, input_to_send: &[u8]) {
            let mut input_to_send_mut = self.input_to_send.borrow_mut();
            *input_to_send_mut = input_to_send.to_vec();
        }

        /// Returns the options received via FIDL requests.
        pub fn get_options(&self) -> fuzz::Options {
            self.options.borrow().clone()
        }

        /// Sets the options to return via FIDL responses.
        pub fn set_options(&self, mut options: fuzz::Options) {
            add_defaults(&mut options);
            let mut options_mut = self.options.borrow_mut();
            *options_mut = options;
        }

        /// Returns the test input received via FIDL requests.
        pub fn get_received_input(&self) -> Vec<u8> {
            self.received_input.borrow().clone()
        }

        /// Reads test input data from a `fuchsia.fuzzer.Input` from a FIDL request.
        async fn receive_input(&self, input: FidlInput) -> Result<()> {
            let mut received_input = Vec::new();
            let mut reader = fidl::AsyncSocket::from_socket(input.socket)?;
            reader.read_to_end(&mut received_input).await?;
            let mut received_input_mut = self.received_input.borrow_mut();
            *received_input_mut = received_input;
            Ok(())
        }

        /// Returns the fuzzing result to be sent via a FIDL response.
        pub fn get_result(&self) -> Result<FuzzResult, zx::Status> {
            self.result.borrow().clone()
        }

        /// Sets the fuzzing result to be sent via a FIDL response.
        pub fn set_result(&self, result: Result<FuzzResult, zx::Status>) {
            let mut result_mut = self.result.borrow_mut();
            *result_mut = result;
        }

        /// Gets the fuzzer status to be sent via FIDL responses.
        pub fn get_status(&self) -> fuzz::Status {
            self.status.borrow().clone()
        }

        /// Sets the fuzzer status to be sent via FIDL responses.
        pub fn set_status(&self, status: fuzz::Status) {
            let mut status_mut = self.status.borrow_mut();
            *status_mut = status;
        }

        // Simulates sending a `msg` to a fuzzer's standard output, standard error, and system log.
        async fn send_output(&self, msg: &str) -> Result<()> {
            let msg_str = format!("{}\n", msg);
            {
                let mut stdout_mut = self.stdout.borrow_mut();
                if let Some(mut stdout) = stdout_mut.take() {
                    stdout.write_all(msg_str.as_bytes()).await?;
                    *stdout_mut = Some(stdout);
                }
            }
            {
                let mut stderr_mut = self.stderr.borrow_mut();
                if let Some(mut stderr) = stderr_mut.take() {
                    stderr.write_all(msg_str.as_bytes()).await?;
                    *stderr_mut = Some(stderr);
                }
            }
            {
                let mut syslog_mut = self.syslog.borrow_mut();
                if let Some(mut syslog) = syslog_mut.take() {
                    send_log_entry(&mut syslog, msg).await?;
                    *syslog_mut = Some(syslog);
                }
            }
            Ok(())
        }

        /// Simulates a long-running workflow being canceled by `fuchsia.fuzzer.Manager/Stop`.
        pub fn cancel(&self) {
            let mut canceled_mut = self.canceled.borrow_mut();
            *canceled_mut = true;
        }

        /// Get whether a simulated call to `fuchsia.fuzzer.Manager/Stop` has been made.
        pub fn is_canceled(&self) -> bool {
            *self.canceled.borrow()
        }
    }

    impl Clone for FakeController {
        fn clone(&self) -> Self {
            Self {
                corpus_type: Rc::clone(&self.corpus_type),
                input_to_send: Rc::clone(&self.input_to_send),
                options: Rc::clone(&self.options),
                received_input: Rc::clone(&self.received_input),
                result: Rc::clone(&self.result),
                status: Rc::clone(&self.status),
                stdout: Rc::clone(&self.stdout),
                stderr: Rc::clone(&self.stderr),
                syslog: Rc::clone(&self.syslog),
                canceled: Rc::clone(&self.canceled),
            }
        }
    }

    /// Serves `fuchsia.fuzzer.Controller` using test fakes.
    pub async fn serve_controller(
        mut stream: fuzz::ControllerRequestStream,
        fake: FakeController,
    ) -> Result<()> {
        let mut _responder = None;
        loop {
            let request = stream.next().await;
            if fake.is_canceled() {
                break;
            }
            match request {
                Some(Ok(fuzz::ControllerRequest::Configure { options, responder })) => {
                    fake.set_options(options);
                    responder.send(zx::Status::OK.into_raw())?;
                }
                Some(Ok(fuzz::ControllerRequest::GetOptions { responder })) => {
                    let options = fake.get_options();
                    responder.send(options)?;
                }
                Some(Ok(fuzz::ControllerRequest::AddToCorpus { corpus, input, responder })) => {
                    fake.receive_input(input).await?;
                    fake.set_corpus_type(corpus);
                    responder.send(zx::Status::OK.into_raw())?;
                }
                Some(Ok(fuzz::ControllerRequest::ReadCorpus {
                    corpus,
                    corpus_reader,
                    responder,
                })) => {
                    fake.set_corpus_type(corpus);
                    let corpus_reader = corpus_reader.into_proxy()?;
                    let input_pair = InputPair::try_from_data(fake.get_input_to_send())?;
                    let (mut fidl_input, input) = input_pair.as_tuple();
                    let corpus_fut = corpus_reader.next(&mut fidl_input);
                    let input_fut = input.send();
                    let results = join!(corpus_fut, input_fut);
                    assert!(results.0.is_ok());
                    assert!(results.1.is_ok());
                    responder.send()?;
                }
                Some(Ok(fuzz::ControllerRequest::GetStatus { responder })) => {
                    let status = fake.get_status();
                    responder.send(status)?;
                }
                Some(Ok(fuzz::ControllerRequest::Execute { test_input, responder })) => {
                    fake.receive_input(test_input).await?;
                    match fake.get_result() {
                        Ok(fuzz_result) => {
                            responder.send(&mut Ok(fuzz_result))?;
                            fake.send_output(fuzz::DONE_MARKER).await?;
                        }
                        Err(status) => {
                            responder.send(&mut Err(status.into_raw()))?;
                        }
                    }
                }
                Some(Ok(fuzz::ControllerRequest::Minimize { test_input, responder })) => {
                    fake.receive_input(test_input).await?;
                    let input_pair = InputPair::try_from_data(fake.get_input_to_send())?;
                    let (fidl_input, input) = input_pair.as_tuple();
                    responder.send(&mut Ok(fidl_input))?;
                    input.send().await?;
                    fake.send_output(fuzz::DONE_MARKER).await?;
                }
                Some(Ok(fuzz::ControllerRequest::Cleanse { test_input, responder })) => {
                    fake.receive_input(test_input).await?;
                    let input_pair = InputPair::try_from_data(fake.get_input_to_send())?;
                    let (fidl_input, input) = input_pair.as_tuple();
                    responder.send(&mut Ok(fidl_input))?;
                    input.send().await?;
                    fake.send_output(fuzz::DONE_MARKER).await?;
                }
                Some(Ok(fuzz::ControllerRequest::Fuzz { responder })) => {
                    // As special cases, fuzzing indefinitely without any errors or fuzzing with an
                    // explicit error of `SHOULD_WAIT` will imitate a FIDL call that does not
                    // complete. These can be interrupted by the shell or allowed to timeout.
                    let result = fake.get_result();
                    let options = fake.get_options();
                    match (options.runs, options.max_total_time, result) {
                        (Some(0), Some(0), Ok(FuzzResult::NoErrors))
                        | (_, _, Err(zx::Status::SHOULD_WAIT)) => {
                            let mut status = fake.get_status();
                            status.running = Some(true);
                            fake.set_status(status);
                            // Prevent the responder being dropped and closing the stream.
                            _responder = Some(responder);
                        }
                        (_, _, Ok(fuzz_result)) => {
                            let input_pair = InputPair::try_from_data(fake.get_input_to_send())?;
                            let (fidl_input, input) = input_pair.as_tuple();
                            let mut response = Ok((fuzz_result, fidl_input));
                            responder.send(&mut response)?;
                            input.send().await?;
                            fake.send_output(fuzz::DONE_MARKER).await?;
                        }
                        (_, _, Err(status)) => {
                            responder.send(&mut Err(status.into_raw()))?;
                        }
                    };
                }
                Some(Ok(fuzz::ControllerRequest::Merge { responder })) => {
                    responder.send(zx::Status::OK.into_raw())?;
                    fake.send_output(fuzz::DONE_MARKER).await?;
                }
                Some(Err(e)) => return Err(anyhow!(e)),
                None => break,
                _ => todo!("not yet implemented"),
            };
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::test_fixtures::perform_test_setup,
        crate::input::test_fixtures::verify_saved,
        crate::input::InputPair,
        crate::test_fixtures::Test,
        crate::util::digest_path,
        anyhow::Result,
        fidl_fuchsia_fuzzer::{self as fuzz, Result_ as FuzzResult},
        fuchsia_zircon_status as zx,
    };

    #[fuchsia::test]
    async fn test_configure() -> Result<()> {
        let test = Test::try_new()?;
        let (fake, controller, _task) = perform_test_setup(&test)?;
        // Modify all the options that start with 'd'.
        let expected = fuzz::Options {
            dictionary_level: Some(1),
            detect_exits: Some(true),
            detect_leaks: Some(false),
            death_exitcode: Some(2),
            debug: Some(true),
            ..fuzz::Options::EMPTY
        };
        controller.configure(expected.clone()).await?;
        let actual = fake.get_options();
        assert_eq!(actual.dictionary_level, expected.dictionary_level);
        assert_eq!(actual.detect_exits, expected.detect_exits);
        assert_eq!(actual.detect_leaks, expected.detect_leaks);
        assert_eq!(actual.death_exitcode, expected.death_exitcode);
        assert_eq!(actual.debug, expected.debug);
        Ok(())
    }

    #[fuchsia::test]
    async fn test_get_options() -> Result<()> {
        let test = Test::try_new()?;
        let (fake, controller, _task) = perform_test_setup(&test)?;
        // Modify all the options that start with 'm'.
        let expected = fuzz::Options {
            max_total_time: Some(20000),
            max_input_size: Some(2000),
            mutation_depth: Some(20),
            malloc_limit: Some(200),
            malloc_exitcode: Some(2),
            ..fuzz::Options::EMPTY
        };
        fake.set_options(expected.clone());
        let actual = controller.get_options().await?;
        assert_eq!(actual.max_total_time, expected.max_total_time);
        assert_eq!(actual.max_input_size, expected.max_input_size);
        assert_eq!(actual.mutation_depth, expected.mutation_depth);
        assert_eq!(actual.malloc_limit, expected.malloc_limit);
        assert_eq!(actual.malloc_exitcode, expected.malloc_exitcode);
        Ok(())
    }

    #[fuchsia::test]
    async fn test_read_corpus() -> Result<()> {
        let test = Test::try_new()?;
        let (fake, controller, _task) = perform_test_setup(&test)?;

        let seed_dir = test.create_dir("seed")?;
        fake.set_input_to_send(b"foo");
        let stats = controller.read_corpus(fuzz::Corpus::Seed, &seed_dir).await?;
        assert_eq!(fake.get_corpus_type(), fuzz::Corpus::Seed);
        assert_eq!(stats.num_inputs, 1);
        assert_eq!(stats.total_size, 3);
        let path = digest_path(&seed_dir, None, b"foo");
        verify_saved(&path, b"foo")?;

        let live_dir = test.create_dir("live")?;
        fake.set_input_to_send(b"barbaz");
        let stats = controller.read_corpus(fuzz::Corpus::Live, &live_dir).await?;
        assert_eq!(fake.get_corpus_type(), fuzz::Corpus::Live);
        assert_eq!(stats.num_inputs, 1);
        assert_eq!(stats.total_size, 6);
        let path = digest_path(&live_dir, None, b"barbaz");
        verify_saved(&path, b"barbaz")?;

        Ok(())
    }

    #[fuchsia::test]
    async fn test_add_to_corpus() -> Result<()> {
        let test = Test::try_new()?;
        let (fake, controller, _task) = perform_test_setup(&test)?;

        let input_pairs: Vec<InputPair> = vec![b"foo".to_vec(), b"bar".to_vec(), b"baz".to_vec()]
            .into_iter()
            .map(|data| InputPair::try_from_data(data).unwrap())
            .collect();
        let stats = controller.add_to_corpus(input_pairs, fuzz::Corpus::Seed).await?;
        assert_eq!(fake.get_corpus_type(), fuzz::Corpus::Seed);
        assert_eq!(stats.num_inputs, 3);
        assert_eq!(stats.total_size, 9);

        let input_pairs: Vec<InputPair> =
            vec![b"qux".to_vec(), b"quux".to_vec(), b"corge".to_vec()]
                .into_iter()
                .map(|data| InputPair::try_from_data(data).unwrap())
                .collect();
        let stats = controller.add_to_corpus(input_pairs, fuzz::Corpus::Live).await?;
        assert_eq!(fake.get_corpus_type(), fuzz::Corpus::Live);
        assert_eq!(stats.num_inputs, 3);
        assert_eq!(stats.total_size, 12);

        Ok(())
    }

    #[fuchsia::test]
    async fn test_get_status() -> Result<()> {
        let test = Test::try_new()?;
        let (fake, controller, _task) = perform_test_setup(&test)?;
        let expected = fuzz::Status {
            running: Some(true),
            runs: Some(1),
            elapsed: Some(2),
            covered_pcs: Some(3),
            covered_features: Some(4),
            corpus_num_inputs: Some(5),
            corpus_total_size: Some(6),
            process_stats: None,
            ..fuzz::Status::EMPTY
        };
        fake.set_status(expected.clone());
        let actual = controller.get_status().await?;
        assert_eq!(actual, expected);
        Ok(())
    }

    #[fuchsia::test]
    async fn test_execute() -> Result<()> {
        let test = Test::try_new()?;
        let (fake, controller, _task) = perform_test_setup(&test)?;

        let input_pair = InputPair::try_from_data(b"foo".to_vec())?;
        let artifact = controller.execute(input_pair).await?;
        assert_eq!(artifact.status, zx::Status::OK);
        assert_eq!(artifact.result, FuzzResult::NoErrors);

        fake.set_result(Ok(FuzzResult::Crash));
        let input_pair = InputPair::try_from_data(b"bar".to_vec())?;
        let artifact = controller.execute(input_pair).await?;
        assert_eq!(artifact.status, zx::Status::OK);
        assert_eq!(artifact.result, FuzzResult::Crash);

        fake.cancel();
        let input_pair = InputPair::try_from_data(b"baz".to_vec())?;
        let artifact = controller.execute(input_pair).await?;
        assert_eq!(artifact.status, zx::Status::CANCELED);
        Ok(())
    }

    #[fuchsia::test]
    async fn test_fuzz() -> Result<()> {
        let test = Test::try_new()?;
        let (fake, controller, _task) = perform_test_setup(&test)?;
        let artifact_dir = test.create_dir("artifacts")?;

        let options = fuzz::Options { runs: Some(10), ..fuzz::Options::EMPTY };
        controller.configure(options).await?;
        let artifact = controller.fuzz(&artifact_dir).await?;
        assert_eq!(artifact.status, zx::Status::OK);
        assert_eq!(artifact.result, FuzzResult::NoErrors);

        fake.set_result(Ok(FuzzResult::Death));
        fake.set_input_to_send(b"foo");
        let artifact = controller.fuzz(&artifact_dir).await?;
        assert_eq!(artifact.status, zx::Status::OK);
        assert_eq!(artifact.result, FuzzResult::Death);
        let path = digest_path(&artifact_dir, Some(FuzzResult::Death), b"foo");
        assert_eq!(path, artifact.path.unwrap());
        verify_saved(&path, b"foo")?;

        fake.cancel();
        let artifact = controller.fuzz(&artifact_dir).await?;
        assert_eq!(artifact.status, zx::Status::CANCELED);
        Ok(())
    }

    #[fuchsia::test]
    async fn test_cleanse() -> Result<()> {
        let test = Test::try_new()?;
        let (fake, controller, _task) = perform_test_setup(&test)?;
        let artifact_dir = test.create_dir("artifacts")?;

        fake.set_input_to_send(b"   bar   ");
        let input_pair = InputPair::try_from_data(b"foobarbaz".to_vec())?;
        let artifact = controller.cleanse(input_pair, &artifact_dir).await?;
        assert_eq!(artifact.status, zx::Status::OK);
        assert_eq!(artifact.result, FuzzResult::Cleansed);
        let path = digest_path(&artifact_dir, Some(FuzzResult::Cleansed), b"   bar   ");
        assert_eq!(path, artifact.path.unwrap());
        verify_saved(&path, b"   bar   ")?;

        fake.cancel();
        let input_pair = InputPair::try_from_data(b"foobarbaz".to_vec())?;
        let artifact = controller.cleanse(input_pair, &artifact_dir).await?;
        assert_eq!(artifact.status, zx::Status::CANCELED);
        Ok(())
    }

    #[fuchsia::test]
    async fn test_minimize() -> Result<()> {
        let test = Test::try_new()?;
        let (fake, controller, _task) = perform_test_setup(&test)?;
        let artifact_dir = test.create_dir("artifacts")?;

        fake.set_input_to_send(b"foo");
        let input_pair = InputPair::try_from_data(b"foofoofoo".to_vec())?;
        let artifact = controller.minimize(input_pair, &artifact_dir).await?;
        assert_eq!(artifact.status, zx::Status::OK);
        assert_eq!(artifact.result, FuzzResult::Minimized);
        let path = digest_path(&artifact_dir, Some(FuzzResult::Minimized), b"foo");
        assert_eq!(path, artifact.path.unwrap());
        verify_saved(&path, b"foo")?;

        fake.cancel();
        let input_pair = InputPair::try_from_data(b"bar".to_vec())?;
        let artifact = controller.minimize(input_pair, &artifact_dir).await?;
        assert_eq!(artifact.status, zx::Status::CANCELED);
        Ok(())
    }

    #[fuchsia::test]
    async fn test_merge() -> Result<()> {
        let test = Test::try_new()?;
        let (fake, controller, _task) = perform_test_setup(&test)?;

        let artifact = controller.merge().await?;
        assert_eq!(artifact.status, zx::Status::OK);
        assert_eq!(artifact.result, FuzzResult::Merged);

        fake.cancel();
        let artifact = controller.merge().await?;
        assert_eq!(artifact.status, zx::Status::CANCELED);
        Ok(())
    }
}
