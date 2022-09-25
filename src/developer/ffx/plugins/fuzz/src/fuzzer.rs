// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::corpus,
    crate::diagnostics::Forwarder,
    crate::input::{save_input, Input},
    crate::options,
    crate::util::{create_artifact_dir, create_corpus_dir, create_dir_at},
    crate::writer::{OutputSink, Writer},
    anyhow::{bail, Context as _, Error, Result},
    fidl::endpoints::create_request_stream,
    fidl_fuchsia_fuzzer::{self as fuzz, Result_ as FuzzResult},
    fuchsia_zircon_status as zx,
    futures::{pin_mut, select, try_join, Future, FutureExt},
    std::path::{Path, PathBuf},
    url::Url,
    walkdir::WalkDir,
};

/// Represents a `fuchsia.fuzzer.Controller` connection to a fuzzer.
#[derive(Debug)]
pub struct Fuzzer<O: OutputSink> {
    url: Url,
    proxy: fuzz::ControllerProxy,
    forwarder: Forwarder<O>,
    output_dir: PathBuf,
    writer: Writer<O>,
}

impl<O: OutputSink> Fuzzer<O> {
    /// Creates a `Fuzzer`.
    ///
    /// The created object maintains a FIDL `proxy` to a fuzzer on a target device. This client
    /// should be created by calling `Manager::connect` with the same `url`. Any fuzzer artifacts
    /// produced by running the fuzzer will be saved to an "artifacts" directory under the given
    /// `output_dir`. Any diagnostic output sent from the fuzzer via `stdout`, `stderr` or `syslog`
    /// will be written using the `writer`.
    pub fn try_new<P: AsRef<Path>>(
        url: &Url,
        proxy: fuzz::ControllerProxy,
        output_dir: P,
        writer: &Writer<O>,
    ) -> Result<Self> {
        let output_dir = PathBuf::from(output_dir.as_ref());
        let logs_dir =
            create_dir_at(&output_dir, "logs").context("failed to create 'logs' directory")?;
        let forwarder =
            Forwarder::try_new(logs_dir, writer).context("failed to create log forwarder")?;
        Ok(Self { proxy, url: url.clone(), output_dir, forwarder, writer: writer.clone() })
    }

    /// Returns the URL of the attached fuzzer.
    pub fn url(&self) -> &Url {
        &self.url
    }

    /// Returns the path where this fuzzer stores artifacts.
    pub fn artifact_dir(&self) -> PathBuf {
        create_artifact_dir(&self.output_dir).unwrap()
    }

    /// Returns the path where this fuzzer stores the corpus of the given |corpus_type|.
    pub fn corpus_dir(&self, corpus_type: fuzz::Corpus) -> PathBuf {
        create_corpus_dir(&self.output_dir, corpus_type).unwrap()
    }

    /// Registers the provided standard output socket with the forwarder.
    pub fn set_output(&mut self, socket: fidl::Socket, output: fuzz::TestOutput) -> Result<()> {
        match output {
            fuzz::TestOutput::Stdout => self.forwarder.set_stdout(socket),
            fuzz::TestOutput::Stderr => self.forwarder.set_stderr(socket),
            fuzz::TestOutput::Syslog => self.forwarder.set_syslog(socket),
            _ => unimplemented!(),
        }
    }

    /// Writes a fuzzer's configured value(s) for one of more option to the internal `Writer`.
    ///
    /// If `name` is `None`, writes out the values of all `fuchsia.fuzzer.Options` for the fuzzer.
    /// Otherwise, only writes the option requested by the given `name`.
    ///
    /// Returns an error if:
    ///   * Communicating with the fuzzer fails
    ///   * `name` is provided but does not match a known option.
    ///
    pub async fn get<S: AsRef<str>>(&self, name: Option<S>) -> Result<()> {
        let fuzz_options = self
            .proxy
            .get_options()
            .await
            .map_err(Error::msg)
            .context("`fuchsia.fuzzer.Controller/GetOptions` failed")?;
        match name {
            Some(name) => {
                let name = name.as_ref();
                let value =
                    options::get(&fuzz_options, name).context("failed to get value for option")?;
                self.writer.println(format!("{}: {}", name, value));
            }
            None => {
                let all = options::get_all(&fuzz_options);
                self.writer.println(format!("{{"));
                let mut first = true;
                for (name, value) in all {
                    if !first {
                        self.writer.println(format!(","));
                    }
                    let quoted = format!("\"{}\": ", name);
                    self.writer.print(format!("  {:<24}{}", quoted, value));
                    first = false;
                }
                self.writer.println(format!(""));
                self.writer.println(format!("}}"));
            }
        };
        Ok(())
    }

    /// Configures the fuzzer to set the `name`d option to the given `value`.
    ///
    /// Returns an error if:
    ///   * Communicating with the fuzzer fails
    ///   * `name` is provided but does not match a known option.
    ///   * `value` cannot be parsed to a valid value for the option.
    ///
    pub async fn set<S: AsRef<str>>(&self, name: S, value: S) -> Result<()> {
        let name = name.as_ref();
        let value = value.as_ref();
        let mut fuzz_options = fuzz::Options::EMPTY;
        // TODO(fxbug.dev/90015): Add flag to read options from a JSON file.
        options::set(&mut fuzz_options, name, value)?;
        self.configure(fuzz_options).await?;
        self.writer.println(format!("Option '{}' set to {}", name, value));
        Ok(())
    }

    /// Adds a test input to one of the fuzzer's corpora.
    ///
    /// Returns an error if:
    ///   * Converting the input to an `Input`/`fuchsia.fuzzer.Input` pair fails.
    ///   * Communicating with the fuzzer fails
    ///   * The fuzzer returns an error, e.g. if it failed to transfer the input.
    ///
    pub async fn add<S: AsRef<str>>(&self, test_input: S, corpus_type: fuzz::Corpus) -> Result<()> {
        self.writer.println(format!("Adding inputs to fuzzer corpus..."));
        let mut input_pairs = Vec::new();
        let path = Path::new(test_input.as_ref());
        match path.is_dir() {
            true => {
                for dir_entry in
                    WalkDir::new(path).follow_links(true).into_iter().filter_map(|e| e.ok())
                {
                    if let Ok(input_pair) = Input::from_path(dir_entry.path()) {
                        input_pairs.push(input_pair);
                    }
                }
            }
            false => {
                let input_pair = Input::from_str(test_input.as_ref(), &self.writer)
                    .context("failed to get input to add")?;
                input_pairs.push(input_pair);
            }
        }
        let mut num_inputs = 0;
        let mut num_bytes = 0;
        for (mut fidl_input, input) in input_pairs.into_iter() {
            num_inputs += 1;
            num_bytes += fidl_input.size;
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
        let units = match num_inputs {
            1 => "input",
            _ => "inputs",
        };
        let corpus_name = corpus::get_name(corpus_type);
        self.writer.println(format!(
            "Added {} {} totaling {} bytes to the {} corpus.",
            num_inputs, units, num_bytes, corpus_name
        ));
        Ok(())
    }

    /// Executes the fuzzer once using the given input.
    ///
    /// Writes the result of execution to this object's internal `Writer`.
    ///
    /// Returns `ZX_ERR_CANCELED` if the workflow was interrupted by a call to
    /// `fuchsia.fuzzer.Controller/Stop`, or `ZX_OK` if it ran to completion.
    ///
    /// Returns an error if:
    ///   * Converting the input to an `Input`/`fuchsia.fuzzer.Input` pair fails.
    ///   * Communicating with the fuzzer fails.
    ///   * The fuzzer returns an error, e.g. it is already performing another workflow.
    ///
    pub async fn try_one<S: AsRef<str>>(&self, test_input: S) -> Result<zx::Status> {
        let (mut fidl_input, input) =
            Input::from_str(test_input, &self.writer).context("failed to get input to try")?;
        self.writer.println(format!("Trying an input of {} bytes...", input.len()));
        let fidl_fut = || async move {
            let result = match self.proxy.execute(&mut fidl_input).await {
                Err(fidl::Error::ClientChannelClosed { status, .. })
                    if status == zx::Status::PEER_CLOSED =>
                {
                    return Ok(zx::Status::CANCELED)
                }
                Err(e) => bail!("`fuchsia.fuzzer.Controller/Execute` failed: {:?}", e),
                Ok(result) => result.map_err(|raw| zx::Status::from_raw(raw)),
            };
            let fuzz_result = match result {
                Err(zx::Status::BAD_STATE) => bail!("another long-running workflow is in progress"),
                Err(status) => {
                    bail!("`fuchsia.fuzzer.Controller/Execute` returned: ZX_ERR_{}", status)
                }
                Ok(fuzz_result) => fuzz_result,
            };
            self.writer.println(get_try_result(&fuzz_result));
            Ok(zx::Status::OK)
        };
        self.with_forwarding(fidl_fut(), Some(input)).await
    }

    /// Runs the fuzzer in a loop to generate and test new inputs.
    ///
    /// The fuzzer will continuously generate new inputs and execute them until one of four
    /// conditions are met:
    ///   * The number of inputs tested exceeds the configured number of `runs`.
    ///   * The configured amount of `time` has elapsed.
    ///   * An input triggers a fatal error, e.g. death by AddressSanitizer.
    ///   * `fuchsia.fuzzer.Controller/Stop` is called.
    ///
    /// Returns `ZX_ERR_CANCELED` if the workflow was interrupted by a call to
    /// `fuchsia.fuzzer.Controller/Stop`, or `ZX_OK` if it ran to completion.
    ///
    /// Returns an error if:
    ///   * Either `runs` or `time` is provided but cannot be parsed to  a valid value.
    ///   * Communicating with the fuzzer fails.
    ///   * The fuzzer returns an error, e.g. it is already performing another workflow.
    ///   * The error-causing input, or "artifact", fails to be received and saved.
    ///
    pub async fn run<S: AsRef<str>>(&self, runs: Option<S>, time: Option<S>) -> Result<zx::Status> {
        self.set_bounds(runs, time).await.context("failed to bound fuzzing")?;
        self.writer.println("Running fuzzer...");
        let fidl_fut = || async move {
            let result = match self.proxy.fuzz().await {
                Err(fidl::Error::ClientChannelClosed { status, .. })
                    if status == zx::Status::PEER_CLOSED =>
                {
                    return Ok(zx::Status::CANCELED);
                }
                Err(e) => bail!("`fuchsia.fuzzer.Controller/Fuzz` failed: {:?}", e),
                Ok(result) => result.map_err(|raw| zx::Status::from_raw(raw)),
            };
            let (fuzz_result, error_input) = match result {
                Err(zx::Status::BAD_STATE) => bail!("another long-running workflow is in progress"),
                Err(status) => {
                    bail!("`fuchsia.fuzzer.Controller/Fuzz` returned: ZX_ERR_{}", status)
                }
                Ok((fuzz_result, error_input)) => (fuzz_result, error_input),
            };
            self.writer.println(get_run_result(&fuzz_result));
            if let Some(prefix) = get_prefix(&fuzz_result) {
                let artifact_dir = self.artifact_dir();
                let path = save_input(error_input, &artifact_dir, Some(prefix))
                    .await
                    .context("failed to save error input")?;
                self.writer.println(format!("Input saved to '{}'", path.to_string_lossy()));
            }
            Ok(zx::Status::OK)
        };
        self.with_forwarding(fidl_fut(), None).await
    }

    /// Replaces bytes in a error-causing input with PII-safe bytes, e.g. spaces.
    ///
    /// The fuzzer will try to reproduce the error caused by the input with each byte replaced by a
    /// fixed number of "clean" candidates.
    ///
    /// Returns `ZX_ERR_CANCELED` if the workflow was interrupted by a call to
    /// `fuchsia.fuzzer.Controller/Stop`, or `ZX_OK` if it ran to completion.
    ///
    /// Returns an error if:
    ///   * Converting the input to an `Input`/`fuchsia.fuzzer.Input` pair fails.
    ///   * Communicating with the fuzzer fails.
    ///   * The fuzzer returns an error, e.g. it is already performing another workflow.
    ///   * The cleansed input fails to be received and saved.
    ///
    pub async fn cleanse<S: AsRef<str>>(&self, test_input: S) -> Result<zx::Status> {
        let (mut fidl_input, input) =
            Input::from_str(test_input, &self.writer).context("failed to get input to cleanse")?;
        self.writer.println(format!("Attempting to cleanse an input of {} bytes...", input.len()));
        let fidl_fut = || async move {
            let result = match self.proxy.cleanse(&mut fidl_input).await {
                Err(fidl::Error::ClientChannelClosed { status, .. })
                    if status == zx::Status::PEER_CLOSED =>
                {
                    return Ok(zx::Status::CANCELED)
                }
                Err(e) => bail!("`fuchsia.fuzzer.Controller/Cleanse` failed: {:?}", e),
                Ok(result) => result.map_err(|raw| zx::Status::from_raw(raw)),
            };
            let cleansed = match result {
                Err(zx::Status::BAD_STATE) => bail!("another long-running workflow is in progress"),
                Err(zx::Status::INVALID_ARGS) => bail!("the provided input did not cause an error"),
                Err(status) => {
                    bail!("`fuchsia.fuzzer.Controller/Cleanse` returned: ZX_ERR_{}", status)
                }
                Ok(cleansed) => cleansed,
            };
            let artifact_dir = self.artifact_dir();
            let path = save_input(cleansed, &artifact_dir, Some("cleansed"))
                .await
                .context("failed to save cleansed input")?;
            self.writer.println(format!("Cleansed input written to '{}'", path.to_string_lossy()));
            Ok(zx::Status::OK)
        };
        self.with_forwarding(fidl_fut(), Some(input)).await
    }

    /// Reduces the length of an error-causing input while preserving the error.
    ///
    /// The fuzzer will bound its attempt to find shorter inputs using the given `runs` or `time`,
    /// if provided, or the defaults from `options::add_defaults`.
    ///
    /// Returns `ZX_ERR_CANCELED` if the workflow was interrupted by a call to
    /// `fuchsia.fuzzer.Controller/Stop`, or `ZX_OK` if it ran to completion.
    ///
    /// Returns an error if:
    ///   * Either `runs` or `time` is provided but cannot be parsed to  a valid value.
    ///   * Converting the input to an `Input`/`fuchsia.fuzzer.Input` pair fails.
    ///   * Communicating with the fuzzer fails.
    ///   * The fuzzer returns an error, e.g. it is already performing another workflow.
    ///   * The minimized input fails to be received and saved.
    ///
    pub async fn minimize<S: AsRef<str>>(
        &self,
        test_input: S,
        runs: Option<S>,
        time: Option<S>,
    ) -> Result<zx::Status> {
        self.set_bounds(runs, time).await.context("failed to bound input minimization")?;
        let (mut fidl_input, input) =
            Input::from_str(test_input, &self.writer).context("failed to get input to minimize")?;
        self.writer.println(format!("Attempting to minimize an input of {} bytes...", input.len()));
        let fidl_fut = || async move {
            let result = match self.proxy.minimize(&mut fidl_input).await {
                Err(fidl::Error::ClientChannelClosed { status, .. })
                    if status == zx::Status::PEER_CLOSED =>
                {
                    return Ok(zx::Status::CANCELED)
                }
                Err(e) => bail!("`fuchsia.fuzzer.Controller/Minimize` failed: {:?}", e),
                Ok(result) => result.map_err(|raw| zx::Status::from_raw(raw)),
            };
            let minimized = match result {
                Err(zx::Status::BAD_STATE) => bail!("another long-running workflow is in progress"),
                Err(zx::Status::INVALID_ARGS) => bail!("the provided input did not cause an error"),
                Err(status) => {
                    bail!("`fuchsia.fuzzer.Controller/Minimize` returned: ZX_ERR_{}", status)
                }
                Ok(minimized) => minimized,
            };
            let artifact_dir = self.artifact_dir();
            let path = save_input(minimized, &artifact_dir, Some("minimized"))
                .await
                .context("failed to save minimized input")?;
            self.writer.println(format!("Minimized input written to '{}'", path.to_string_lossy()));
            Ok(zx::Status::OK)
        };
        self.with_forwarding(fidl_fut(), Some(input)).await
    }

    /// Removes inputs from the corpus that produce duplicate coverage.
    ///
    /// The fuzzer makes a finite number of passes over its seed and live corpora. The seed corpus
    /// is unchanged, but the fuzzer will try to find the set of shortest inputs that preserves
    /// coverage. Once complete, the compacted fuzzer is saved to the `corpus_dir`, if provided, or
    /// the current working directory.
    ///
    /// Returns `ZX_ERR_CANCELED` if the workflow was interrupted by a call to
    /// `fuchsia.fuzzer.Controller/Stop`, or `ZX_OK` if it ran to completion.
    ///
    /// Returns an error if:
    ///   * Communicating with the fuzzer fails.
    ///   * The fuzzer returns an error, e.g. it is already performing another workflow.
    ///   * One or more inputs fails to be received and saved.
    ///
    pub async fn merge(&self) -> Result<zx::Status> {
        self.writer.println("Compacting fuzzer corpus...");
        let fidl_fut = || async move {
            let status = match self.proxy.merge().await {
                Err(fidl::Error::ClientChannelClosed { status, .. })
                    if status == zx::Status::PEER_CLOSED =>
                {
                    return Ok(zx::Status::CANCELED)
                }
                Err(e) => bail!("`fuchsia.fuzzer.Controller/Merge` failed: {:?}", e),
                Ok(raw) => zx::Status::from_raw(raw),
            };
            match status {
                zx::Status::OK => {}
                zx::Status::BAD_STATE => bail!("another long-running workflow is in progress"),
                zx::Status::INVALID_ARGS => bail!("an input in the seed corpus triggered an error"),
                status => bail!("`fuchsia.fuzzer.Controller/Merge` returned: ZX_ERR_{}", status),
            };
            self.fetch(fuzz::Corpus::Live).await?;
            Ok(zx::Status::OK)
        };
        self.with_forwarding(fidl_fut(), None).await
    }

    /// Returns information about fuzzer execution.
    ///
    /// The status typically includes information such as how long the fuzzer has been running, how
    /// many edges in the call graph have been covered, how large the corpus is, etc.
    ///
    /// Refer to `fuchsia.fuzzer.Status` for precise details on the returned information.
    ///
    pub async fn status(&self) -> Result<fuzz::Status> {
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

    /// Retrieves test inputs from one of the fuzzer's corpora.
    ///
    /// The compacted corpus is saved to the `corpus_dir`, if provided, or the current working
    /// directory.
    ///
    /// Returns an error if:
    ///   * Communicating with the fuzzer fails.
    ///   * One or more inputs fails to be received and saved.
    ///
    pub async fn fetch(&self, corpus_type: fuzz::Corpus) -> Result<()> {
        self.writer.println(format!("Retrieving fuzzer corpus..."));
        let corpus_dir = self.corpus_dir(corpus_type);
        let (client_end, stream) = create_request_stream::<fuzz::CorpusReaderMarker>()
            .context("failed to create fuchsia.fuzzer.CorpusReader stream")?;
        let (_, corpus_stats) = try_join!(
            async { self.proxy.read_corpus(corpus_type, client_end).await.map_err(Error::msg) },
            async { corpus::read(stream, corpus_dir).await },
        )
        .context("`fuchsia.fuzzer.Controller/ReadCorpus` failed")?;
        let units = match corpus_stats.num_inputs {
            1 => "input",
            _ => "inputs",
        };
        let corpus_name = corpus::get_name(corpus_type);
        self.writer.println(format!(
            "Retrieved {} {} totaling {} bytes from the {} corpus.",
            corpus_stats.num_inputs, units, corpus_stats.total_size, corpus_name
        ));
        Ok(())
    }

    // Runs the given |fidl_fut| along with futures to optionally send an |input| and forward
    // fuzzer output.
    async fn with_forwarding<F>(&self, fidl_fut: F, input: Option<Input>) -> Result<zx::Status>
    where
        F: Future<Output = Result<zx::Status>>,
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
        pin_mut!(fidl_fut, send_fut, forward_fut);
        loop {
            select! {
                result = fidl_fut => {
                    let status = result?;
                    // If `fidl_fut` completes with e.g. `Ok(zx::Status::CANCELED)`, drop the
                    // `send_fut` and `forward_fut` futures.
                    if status != zx::Status::OK {
                        return Ok(status);
                    }
                }
                result = send_fut => {
                    result?;
                }
                result = forward_fut => {
                    result?;
                }
                complete => return Ok(zx::Status::OK),
            };
        }
    }

    // Helper methods for configuring the fuzzer.

    async fn configure(&self, options: fuzz::Options) -> Result<()> {
        self.writer.println("Configuring fuzzer...");
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

    async fn set_bounds<S: AsRef<str>>(&self, runs: Option<S>, time: Option<S>) -> Result<()> {
        let mut fuzz_options = fuzz::Options::EMPTY;
        if let Some(runs) = runs {
            options::set(&mut fuzz_options, "runs", runs.as_ref())
                .context("failed to set 'runs'")?;
        }
        if let Some(time) = time {
            options::set(&mut fuzz_options, "max_total_time", time.as_ref())
                .context("failed to set 'max_total_time'")?;
        }
        self.configure(fuzz_options).await
    }
}

fn get_prefix(result: &FuzzResult) -> Option<&str> {
    match result {
        FuzzResult::NoErrors => None,
        FuzzResult::BadMalloc => Some("alloc"),
        FuzzResult::Crash => Some("crash"),
        FuzzResult::Death => Some("death"),
        FuzzResult::Exit => Some("exit"),
        FuzzResult::Leak => Some("leak"),
        FuzzResult::Oom => Some("oom"),
        FuzzResult::Timeout => Some("timeout"),
        _ => unreachable!(),
    }
}

fn get_try_result(result: &FuzzResult) -> String {
    format!("The input {}", get_result(result))
}

fn get_run_result(result: &FuzzResult) -> String {
    match result {
        FuzzResult::NoErrors => "No input to the fuzzer caused an error.".to_string(),
        result => format!("An input to the fuzzer {}", get_result(result)),
    }
}

fn get_result(result: &FuzzResult) -> &str {
    match result {
        FuzzResult::NoErrors => "did not cause any errors.",
        FuzzResult::BadMalloc => "caused an invalid allocation of memory.",
        FuzzResult::Crash => "caused a process to crash.",
        FuzzResult::Death => "triggered a sanitizer violation.",
        FuzzResult::Exit => "caused a process to exit unexpectedly.",
        FuzzResult::Leak => "caused a process to leak memory.",
        FuzzResult::Oom => "caused a process to exhaust memory.",
        FuzzResult::Timeout => "caused a process to time out without returning.",
        _ => unreachable!(),
    }
}

#[cfg(test)]
pub mod test_fixtures {
    use {
        super::Fuzzer,
        crate::diagnostics::test_fixtures::send_log_entry,
        crate::input::Input,
        crate::options::test_fixtures::add_defaults,
        crate::test_fixtures::{create_task, Test, TEST_URL},
        crate::writer::test_fixtures::BufferSink,
        anyhow::{anyhow, Context as _, Result},
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_fuzzer::{self as fuzz, Input as FidlInput, Result_ as FuzzResult},
        fuchsia_async as fasync, fuchsia_zircon_status as zx,
        futures::{join, AsyncReadExt, AsyncWriteExt, StreamExt},
        std::cell::RefCell,
        std::rc::Rc,
        url::Url,
    };

    /// Creates a test setup suitable for unit testing `Fuzzer`.
    ///
    /// On success, returns a tuple of a `FakeFuzzer`, a `Fuzzer`, and a `Task`. The task will serve
    /// a `fuchsia.fuzzer.Controller` stream until it is dropped. The fuzzer holds the other end of
    /// this FIDL channel in its `proxy` field. The test fake has various fields that it shares with
    /// the controller task; these can be accessed or mutated by unit tests to check what was sent
    /// via FIDL requests or what should be sent in FIDL responses. Both the test fake and the
    /// fuzzer are configured to write to the test `output`.
    ///
    /// Returns an error if it fails to create or associate any of the objects with each other.
    ///
    pub fn perform_test_setup(
        test: &Test,
    ) -> Result<(FakeFuzzer, Fuzzer<BufferSink>, fasync::Task<()>)> {
        let url = Url::parse(TEST_URL).context("failed to parse URL")?;
        let (proxy, stream) = create_proxy_and_stream::<fuzz::ControllerMarker>()
            .context("failed to create FIDL connection")?;
        let fake = FakeFuzzer::new();
        let writer = test.writer();
        let fuzzer = Fuzzer::try_new(&url, proxy, test.root_dir(), &writer)?;
        let task = create_task(serve_controller(stream, fake.clone()), writer.clone());
        Ok((fake, fuzzer, task))
    }

    /// Test fake that allows configuring how to respond to `fuchsia.fuzzer.Controller` methods.
    ///
    /// Rhese fields are Rc<RefCell<_>> in order to be cloned and shared with the `Task` serving the
    /// controller. Unit tests can use this object to query values passed in FIDL requests and set
    /// values returned by FIDL responses.
    #[derive(Debug)]
    pub struct FakeFuzzer {
        url: Rc<RefCell<Option<String>>>,
        corpus_type: Rc<RefCell<fuzz::Corpus>>,
        input_to_send: Rc<RefCell<Vec<u8>>>,
        options: Rc<RefCell<fuzz::Options>>,
        received_input: Rc<RefCell<Vec<u8>>>,
        result: Rc<RefCell<Result<FuzzResult, zx::Status>>>,
        status: Rc<RefCell<fuzz::Status>>,
        stdout: Rc<RefCell<Option<fasync::Socket>>>,
        stderr: Rc<RefCell<Option<fasync::Socket>>>,
        syslog: Rc<RefCell<Option<fasync::Socket>>>,
    }

    impl FakeFuzzer {
        /// Creates a fake fuzzer that can serve `fuchsia.fuzzer.Controller`.
        pub fn new() -> Self {
            let status = fuzz::Status { running: Some(false), ..fuzz::Status::EMPTY };
            let mut options = fuzz::Options::EMPTY;
            add_defaults(&mut options);
            Self {
                url: Rc::new(RefCell::new(None)),
                corpus_type: Rc::new(RefCell::new(fuzz::Corpus::Seed)),
                input_to_send: Rc::new(RefCell::new(Vec::new())),
                options: Rc::new(RefCell::new(options)),
                received_input: Rc::new(RefCell::new(Vec::new())),
                result: Rc::new(RefCell::new(Ok(FuzzResult::NoErrors))),
                status: Rc::new(RefCell::new(status)),
                stdout: Rc::new(RefCell::new(None)),
                stderr: Rc::new(RefCell::new(None)),
                syslog: Rc::new(RefCell::new(None)),
            }
        }

        /// Returns the fuzzer's URL as a string, if set.
        pub fn url(&self) -> Option<String> {
            self.url.borrow().clone()
        }

        /// Sets the URL of the fuzzer.
        pub fn set_url<S: AsRef<str>>(&self, url: S) {
            let mut url_mut = self.url.borrow_mut();
            *url_mut = Some(url.as_ref().to_string());
        }

        /// Simulates a call to `fuchsia.fuzzer.Manager/GetOutput` without a `fuzz-manager`.
        pub fn set_output(&self, output: fuzz::TestOutput, socket: fidl::Socket) -> i32 {
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
            zx::Status::OK.into_raw()
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

        /// Simulates sending a `msg` to a fuzzer's standard output, standard error, and system log.
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
    }

    impl Clone for FakeFuzzer {
        fn clone(&self) -> Self {
            Self {
                url: Rc::clone(&self.url),
                corpus_type: Rc::clone(&self.corpus_type),
                input_to_send: Rc::clone(&self.input_to_send),
                options: Rc::clone(&self.options),
                received_input: Rc::clone(&self.received_input),
                result: Rc::clone(&self.result),
                status: Rc::clone(&self.status),
                stdout: Rc::clone(&self.stdout),
                stderr: Rc::clone(&self.stderr),
                syslog: Rc::clone(&self.syslog),
            }
        }
    }

    /// Serves `fuchsia.fuzzer.Controller` using test fakes.
    pub async fn serve_controller(
        mut stream: fuzz::ControllerRequestStream,
        fake: FakeFuzzer,
    ) -> Result<()> {
        let mut _responder = None;
        loop {
            match stream.next().await {
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
                    corpus: _,
                    corpus_reader,
                    responder,
                })) => {
                    let corpus_reader = corpus_reader.into_proxy()?;
                    let (mut fidl_input, input) = Input::create(fake.get_input_to_send())?;
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
                    let (fidl_input, input) = Input::create(fake.get_input_to_send())?;
                    responder.send(&mut Ok(fidl_input))?;
                    input.send().await?;
                    fake.send_output(fuzz::DONE_MARKER).await?;
                }
                Some(Ok(fuzz::ControllerRequest::Cleanse { test_input, responder })) => {
                    fake.receive_input(test_input).await?;
                    let (fidl_input, input) = Input::create(fake.get_input_to_send())?;
                    responder.send(&mut Ok(fidl_input))?;
                    input.send().await?;
                    fake.send_output(fuzz::DONE_MARKER).await?;
                }
                Some(Ok(fuzz::ControllerRequest::Fuzz { responder })) => {
                    // As a special case, fuzzing indefinitely without any errors will imitate a
                    // FIDL call that does not complete, and that can be interrupted by the shell.
                    let result = fake.get_result();
                    let options = fake.get_options();
                    match (options.runs, options.max_total_time, result) {
                        (Some(0), Some(0), Ok(FuzzResult::NoErrors)) => {
                            let mut status = fake.get_status();
                            status.running = Some(true);
                            fake.set_status(status);
                            // Prevent the responder being dropped and closing the stream.
                            _responder = Some(responder);
                        }
                        (_, _, Ok(fuzz_result)) => {
                            let (fidl_input, input) = Input::create(fake.get_input_to_send())?;
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
                None => return Ok(()),
                _ => todo!("not yet implemented"),
            };
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::test_fixtures::{perform_test_setup, FakeFuzzer},
        super::{get_prefix, get_run_result, get_try_result, Fuzzer},
        crate::input::test_fixtures::verify_saved,
        crate::options,
        crate::options::test_fixtures::add_defaults,
        crate::test_fixtures::Test,
        crate::util::digest_path,
        crate::writer::test_fixtures::BufferSink,
        anyhow::Result,
        fidl_fuchsia_fuzzer::{self as fuzz, Result_ as FuzzResult},
        fuchsia_zircon_status as zx,
    };

    #[fuchsia::test]
    async fn test_get_one() -> Result<()> {
        let mut test = Test::try_new()?;
        let (fake, fuzzer, _task) = perform_test_setup(&test)?;
        let options = fuzz::Options { runs: Some(123), ..fuzz::Options::EMPTY };
        fake.set_options(options);
        fuzzer.get(Some("runs".to_string())).await?;
        test.output_matches("runs: 123");
        assert!(fuzzer.get(Some("nonsense".to_string())).await.is_err());
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_get_all() -> Result<()> {
        let mut test = Test::try_new()?;
        let (_fake, fuzzer, _task) = perform_test_setup(&test)?;
        fuzzer.get(None::<&str>).await?;
        test.output_matches("{");
        test.output_matches("  \"runs\":                 0,");
        test.output_matches("  \"max_total_time\":       0,");
        test.output_matches("  \"seed\":                 0,");
        test.output_matches("  \"max_input_size\":       \"1mb\",");
        test.output_matches("  \"mutation_depth\":       5,");
        test.output_matches("  \"dictionary_level\":     0,");
        test.output_matches("  \"detect_exits\":         false,");
        test.output_matches("  \"detect_leaks\":         false,");
        test.output_matches("  \"run_limit\":            \"20m\",");
        test.output_matches("  \"malloc_limit\":         \"2gb\",");
        test.output_matches("  \"oom_limit\":            \"2gb\",");
        test.output_matches("  \"purge_interval\":       \"1s\",");
        test.output_matches("  \"malloc_exitcode\":      2000,");
        test.output_matches("  \"death_exitcode\":       2001,");
        test.output_matches("  \"leak_exitcode\":        2002,");
        test.output_matches("  \"oom_exitcode\":         2003,");
        test.output_matches("  \"pulse_interval\":       \"20s\",");
        test.output_matches("  \"debug\":                false,");
        test.output_matches("  \"print_final_stats\":    false,");
        test.output_matches("  \"use_value_profile\":    false,");
        test.output_matches("  \"asan_options\":         \"\",");
        test.output_matches("  \"ubsan_options\":        \"\"");
        test.output_matches("}");
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_set() -> Result<()> {
        let mut test = Test::try_new()?;
        let (_fake, fuzzer, _task) = perform_test_setup(&test)?;
        fuzzer.set("runs", "10").await?;
        test.output_matches("Configuring fuzzer...");
        test.output_matches("Option 'runs' set to 10");

        assert!(fuzzer.set("invalid", "nonsense").await.is_err());
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_add() -> Result<()> {
        let mut test = Test::try_new()?;
        let (fake, fuzzer, _task) = perform_test_setup(&test)?;
        let test_files = vec!["input1", "input2", "input3"];

        let corpus_dir = test.create_dir("corpus")?;
        test.create_test_files(&corpus_dir, test_files.iter())?;

        fuzzer.add(format!("{}/input1", corpus_dir.to_string_lossy()), fuzz::Corpus::Seed).await?;
        test.output_matches("Adding inputs to fuzzer corpus...");
        test.output_matches("Added 1 input totaling 6 bytes to the seed corpus.");
        assert_eq!(fake.get_corpus_type(), fuzz::Corpus::Seed);
        assert_eq!(fake.get_received_input(), b"input1");

        fuzzer.add("666f6f", fuzz::Corpus::Live).await?;
        test.output_matches("Adding inputs to fuzzer corpus...");
        test.output_matches("Added 1 input totaling 3 bytes to the live corpus.");
        assert_eq!(fake.get_corpus_type(), fuzz::Corpus::Live);
        assert_eq!(fake.get_received_input(), b"foo");

        fuzzer.add(corpus_dir.to_string_lossy().to_string(), fuzz::Corpus::Live).await?;
        test.output_matches("Adding inputs to fuzzer corpus...");
        test.output_matches("Added 3 inputs totaling 18 bytes to the live corpus.");
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_try() -> Result<()> {
        async fn test_try_one(
            fake: &FakeFuzzer,
            fuzzer: &Fuzzer<BufferSink>,
            input_data: &[u8],
            result: FuzzResult,
            test: &mut Test,
        ) -> Result<()> {
            let test_input = hex::encode(input_data);
            fake.set_result(Ok(result));
            let status = fuzzer.try_one(test_input).await?;
            assert_eq!(status, zx::Status::OK);
            test.output_matches(format!("Trying an input of {} bytes...", input_data.len()));
            test.output_matches(get_try_result(&result));
            let received_input = fake.get_received_input();
            assert_eq!(received_input, input_data);
            Ok(())
        }

        let mut test = Test::try_new()?;
        let (fake, fuzzer, _task) = perform_test_setup(&test)?;
        test_try_one(&fake, &fuzzer, b"no errors", FuzzResult::NoErrors, &mut test).await?;
        test_try_one(&fake, &fuzzer, b"bad malloc", FuzzResult::BadMalloc, &mut test).await?;
        test_try_one(&fake, &fuzzer, b"crash", FuzzResult::Crash, &mut test).await?;
        test_try_one(&fake, &fuzzer, b"death", FuzzResult::Death, &mut test).await?;
        test_try_one(&fake, &fuzzer, b"exit", FuzzResult::Exit, &mut test).await?;
        test_try_one(&fake, &fuzzer, b"leak", FuzzResult::Leak, &mut test).await?;
        test_try_one(&fake, &fuzzer, b"oom", FuzzResult::Oom, &mut test).await?;
        test_try_one(&fake, &fuzzer, b"timeout", FuzzResult::Timeout, &mut test).await?;
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_run() -> Result<()> {
        async fn test_run_once(
            fake: &FakeFuzzer,
            fuzzer: &Fuzzer<BufferSink>,
            runs: Option<String>,
            time: Option<String>,
            input_data: &[u8],
            result: FuzzResult,
            test: &mut Test,
        ) -> Result<Option<String>> {
            fake.set_result(Ok(result));
            fake.set_input_to_send(input_data);
            let status = fuzzer.run(runs.clone(), time.clone()).await?;
            assert_eq!(status, zx::Status::OK);
            test.output_matches("Configuring fuzzer...");
            test.output_matches("Running fuzzer...");
            let mut expected = fuzz::Options::EMPTY;
            add_defaults(&mut expected);
            if let Some(runs) = runs {
                options::set(&mut expected, "runs", &runs)?;
            }
            if let Some(time) = time {
                options::set(&mut expected, "max_total_time", &time)?;
            }
            let actual = fake.get_options();
            assert_eq!(actual.runs, expected.runs);
            assert_eq!(actual.max_total_time, expected.max_total_time);
            test.output_matches(get_run_result(&result));
            match result {
                FuzzResult::NoErrors => Ok(None),
                result => {
                    let prefix = get_prefix(&result);
                    let artifact = digest_path(fuzzer.artifact_dir(), prefix, input_data);
                    verify_saved(&artifact, input_data)?;
                    let artifact = artifact.to_string_lossy().to_string();
                    test.output_matches(format!("Input saved to '{}'", artifact));
                    Ok(Some(artifact))
                }
            }
        }

        let mut test = Test::try_new()?;
        let (fake, fuzzer, _task) = perform_test_setup(&test)?;

        let runs = Some("10".to_string());
        let time = Some("10s".to_string());
        test_run_once(&fake, &fuzzer, runs, None, b"", FuzzResult::NoErrors, &mut test).await?;
        test_run_once(&fake, &fuzzer, None, time, b"", FuzzResult::NoErrors, &mut test).await?;
        test_run_once(&fake, &fuzzer, None, None, b"", FuzzResult::BadMalloc, &mut test).await?;
        test_run_once(&fake, &fuzzer, None, None, b"", FuzzResult::Crash, &mut test).await?;
        test_run_once(&fake, &fuzzer, None, None, b"", FuzzResult::Death, &mut test).await?;
        test_run_once(&fake, &fuzzer, None, None, b"", FuzzResult::Exit, &mut test).await?;
        test_run_once(&fake, &fuzzer, None, None, b"", FuzzResult::Leak, &mut test).await?;
        test_run_once(&fake, &fuzzer, None, None, b"", FuzzResult::Oom, &mut test).await?;
        test_run_once(&fake, &fuzzer, None, None, b"", FuzzResult::Timeout, &mut test).await?;
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_cleanse() -> Result<()> {
        let mut test = Test::try_new()?;
        let (fake, fuzzer, _task) = perform_test_setup(&test)?;
        let test_input = hex::encode("hello");
        fake.set_input_to_send(b"world");
        let status = fuzzer.cleanse(test_input).await?;
        assert_eq!(status, zx::Status::OK);
        test.output_matches("Attempting to cleanse an input of 5 bytes...");
        let artifact = digest_path(fuzzer.artifact_dir(), Some("cleansed"), b"world");
        verify_saved(&artifact, b"world")?;
        test.output_matches(format!("Cleansed input written to '{}'", artifact.to_string_lossy()));
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_minimize() -> Result<()> {
        let mut test = Test::try_new()?;
        let (fake, fuzzer, _task) = perform_test_setup(&test)?;
        let test_input = hex::encode("hello");

        fake.set_input_to_send(b"world");
        let status = fuzzer.minimize(test_input.clone(), None, None).await?;
        assert_eq!(status, zx::Status::OK);
        test.output_matches("Configuring fuzzer...");
        test.output_matches("Attempting to minimize an input of 5 bytes...");
        let artifact = digest_path(fuzzer.artifact_dir(), Some("minimized"), b"world");
        verify_saved(&artifact, b"world")?;
        test.output_matches(format!("Minimized input written to '{}'", artifact.to_string_lossy()));

        fake.set_input_to_send(b"world");
        let runs = "10";
        let time = "10s";
        let status =
            fuzzer.minimize(test_input, Some(runs.to_string()), Some(time.to_string())).await?;
        assert_eq!(status, zx::Status::OK);
        test.output_matches("Configuring fuzzer...");
        test.output_matches("Attempting to minimize an input of 5 bytes...");
        let artifact = digest_path(fuzzer.artifact_dir(), Some("minimized"), b"world");
        verify_saved(&artifact, b"world")?;
        test.output_matches(format!("Minimized input written to '{}'", artifact.to_string_lossy()));
        let actual = fake.get_options();
        let mut expected = fuzz::Options::EMPTY;
        options::set(&mut expected, "runs", runs)?;
        options::set(&mut expected, "max_total_time", time)?;
        assert_eq!(actual.runs, expected.runs);
        assert_eq!(actual.max_total_time, expected.max_total_time);
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_merge() -> Result<()> {
        let mut test = Test::try_new()?;
        let (fake, fuzzer, _task) = perform_test_setup(&test)?;

        fake.set_input_to_send(b"hello");
        let status = fuzzer.merge().await?;
        assert_eq!(status, zx::Status::OK);
        test.output_matches("Compacting fuzzer corpus...");
        test.output_matches("Retrieving fuzzer corpus...");
        test.output_matches("Retrieved 1 input totaling 5 bytes from the live corpus.");
        let input = digest_path(fuzzer.corpus_dir(fuzz::Corpus::Live), None, b"hello");
        verify_saved(&input, b"hello")?;
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_fetch() -> Result<()> {
        let mut test = Test::try_new()?;
        let (fake, fuzzer, _task) = perform_test_setup(&test)?;

        fake.set_input_to_send(b"world");
        fuzzer.fetch(fuzz::Corpus::Seed).await?;
        test.output_matches("Retrieving fuzzer corpus...");
        test.output_matches("Retrieved 1 input totaling 5 bytes from the seed corpus.");
        let input = digest_path(fuzzer.corpus_dir(fuzz::Corpus::Seed), None, b"world");
        verify_saved(&input, b"world")?;
        test.verify_output()
    }
}
