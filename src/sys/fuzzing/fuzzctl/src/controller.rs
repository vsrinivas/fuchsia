// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::artifact::Artifact,
    crate::constants::*,
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
    std::cmp::max,
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
                    *timeout_mut = Some(max(n * 2, 60 * NANOS_PER_SECOND));
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
        let expected_num_inputs = input_pairs.len();
        let expected_total_size =
            input_pairs.iter().fold(0, |total, input_pair| total + input_pair.len());
        let mut corpus_stats = corpus::Stats { num_inputs: 0, total_size: 0 };
        for input_pair in input_pairs.into_iter() {
            let (mut fidl_input, input) = input_pair.as_tuple();
            let fidl_input_size = fidl_input.size;
            let (raw, _) = try_join!(
                async {
                    self.proxy.add_to_corpus(corpus_type, &mut fidl_input).await.map_err(Error::msg)
                },
                input.send(),
            )
            .context("`fuchsia.fuzzer.Controller/AddToCorpus` failed")?;
            match zx::Status::from_raw(raw) {
                zx::Status::OK => {
                    corpus_stats.num_inputs += 1;
                    corpus_stats.total_size += fidl_input_size;
                }
                status => {
                    bail!(
                        "`fuchsia.fuzzer.Controller/AddToCorpus` returned: ZX_ERR_{} \
                           after writing {} of {} files ({} of {} bytes)",
                        status,
                        corpus_stats.num_inputs,
                        expected_num_inputs,
                        corpus_stats.total_size,
                        expected_total_size
                    )
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
mod tests {
    use {
        crate::util::digest_path,
        anyhow::{Context as _, Result},
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_fuzzer::{self as fuzz, Result_ as FuzzResult},
        fuchsia_async as fasync,
        fuchsia_fuzzctl::{Controller, InputPair},
        fuchsia_fuzzctl_test::{create_task, serve_controller, verify_saved, FakeController, Test},
        fuchsia_zircon_status as zx,
    };

    // Creates a test setup suitable for unit testing `Controller`.
    fn perform_test_setup(
        test: &Test,
    ) -> Result<(FakeController, fuzz::ControllerProxy, fasync::Task<()>)> {
        let fake = test.controller();
        let (proxy, stream) = create_proxy_and_stream::<fuzz::ControllerMarker>()
            .context("failed to create FIDL connection")?;
        let task = create_task(serve_controller(stream, test.clone()), test.writer());
        Ok((fake, proxy, task))
    }

    #[fuchsia::test]
    async fn test_configure() -> Result<()> {
        let test = Test::try_new()?;
        let (fake, proxy, _task) = perform_test_setup(&test)?;
        let controller = Controller::new(proxy, test.writer());

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
        let (fake, proxy, _task) = perform_test_setup(&test)?;
        let controller = Controller::new(proxy, test.writer());

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
        let (fake, proxy, _task) = perform_test_setup(&test)?;
        let controller = Controller::new(proxy, test.writer());

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
        let (fake, proxy, _task) = perform_test_setup(&test)?;
        let controller = Controller::new(proxy, test.writer());

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
        let (fake, proxy, _task) = perform_test_setup(&test)?;
        let controller = Controller::new(proxy, test.writer());

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
        let (fake, proxy, _task) = perform_test_setup(&test)?;
        let controller = Controller::new(proxy, test.writer());

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
        let (fake, proxy, _task) = perform_test_setup(&test)?;
        let controller = Controller::new(proxy, test.writer());
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
        let (fake, proxy, _task) = perform_test_setup(&test)?;
        let controller = Controller::new(proxy, test.writer());
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
        let (fake, proxy, _task) = perform_test_setup(&test)?;
        let controller = Controller::new(proxy, test.writer());
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
        let (fake, proxy, _task) = perform_test_setup(&test)?;
        let controller = Controller::new(proxy, test.writer());

        let artifact = controller.merge().await?;
        assert_eq!(artifact.status, zx::Status::OK);
        assert_eq!(artifact.result, FuzzResult::Merged);

        fake.cancel();
        let artifact = controller.merge().await?;
        assert_eq!(artifact.status, zx::Status::CANCELED);

        Ok(())
    }
}
