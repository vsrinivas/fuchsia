// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::options,
    anyhow::{Context as _, Result},
    fidl_fuchsia_fuzzer::{self as fuzz, Result_ as FuzzResult},
    fuchsia_fuzzctl::{
        create_artifact_dir, create_corpus_dir, create_dir_at, get_corpus_name, Controller,
        InputPair, OutputSink, Writer,
    },
    fuchsia_zircon_status as zx,
    std::path::{Path, PathBuf},
    url::Url,
    walkdir::WalkDir,
};

/// Represents a `fuchsia.fuzzer.Controller` connection to a fuzzer.
#[derive(Debug)]
pub struct Fuzzer<O: OutputSink> {
    url: Url,
    controller: Controller<O>,
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
    pub fn new<P: AsRef<Path>>(
        url: &Url,
        proxy: fuzz::ControllerProxy,
        output_dir: P,
        writer: &Writer<O>,
    ) -> Self {
        Self {
            url: url.clone(),
            controller: Controller::new(proxy, &writer),
            output_dir: PathBuf::from(output_dir.as_ref()),
            writer: writer.clone(),
        }
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
        let logs_dir =
            create_dir_at(&self.output_dir, "logs").context("failed to create 'logs' directory")?;
        self.controller.set_output(socket, output, &Some(logs_dir))
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
        let fuzz_options = self.controller.get_options().await?;
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
        self.writer.println("Configuring fuzzer...");
        self.controller.configure(fuzz_options).await?;
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
                    if let Ok(input_pair) = InputPair::try_from_path(dir_entry.path()) {
                        input_pairs.push(input_pair);
                    }
                }
            }
            false => {
                let input_pair = InputPair::try_from_str(test_input.as_ref(), &self.writer)
                    .context("failed to get input to add")?;
                input_pairs.push(input_pair);
            }
        }
        let corpus_stats = self.controller.add_to_corpus(input_pairs, corpus_type).await?;
        let units = match corpus_stats.num_inputs {
            1 => "input",
            _ => "inputs",
        };
        let corpus_name = get_corpus_name(corpus_type);
        self.writer.println(format!(
            "Added {} {} totaling {} bytes to the {} corpus.",
            corpus_stats.num_inputs, units, corpus_stats.total_size, corpus_name
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
        let input_pair = InputPair::try_from_str(test_input, &self.writer)
            .context("failed to get input to try")?;
        self.writer.println(format!("Trying an input of {} bytes...", input_pair.len()));
        let artifact = self.controller.execute(input_pair).await?;
        if artifact.status != zx::Status::OK {
            return Ok(artifact.status);
        }
        self.writer.println(get_try_result(&artifact.result));
        Ok(zx::Status::OK)
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
        let artifact_dir = self.artifact_dir();
        let artifact = self.controller.fuzz(&artifact_dir).await?;
        if artifact.status != zx::Status::OK {
            return Ok(artifact.status);
        }
        self.writer.println(get_run_result(&artifact.result));
        if let Some(path) = artifact.path {
            self.writer.println(format!("Input saved to '{}'", path.to_string_lossy()));
        }
        Ok(zx::Status::OK)
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
        let input_pair = InputPair::try_from_str(test_input, &self.writer)
            .context("failed to get input to cleanse")?;
        self.writer
            .println(format!("Attempting to cleanse an input of {} bytes...", input_pair.len()));
        let artifact_dir = self.artifact_dir();
        let artifact = self.controller.cleanse(input_pair, &artifact_dir).await?;
        if artifact.status != zx::Status::OK {
            return Ok(artifact.status);
        }
        if let Some(path) = artifact.path {
            self.writer.println(format!("Cleansed input written to '{}'", path.to_string_lossy()));
        };
        Ok(zx::Status::OK)
    }

    /// Reduces the length of an error-causing input while preserving the error.
    ///
    /// The fuzzer will bound its attempt to find shorter inputs using the given `runs` or `time`,
    /// if provided.
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
        let input_pair = InputPair::try_from_str(test_input, &self.writer)
            .context("failed to get input to minimize")?;
        self.writer
            .println(format!("Attempting to minimize an input of {} bytes...", input_pair.len()));
        let artifact_dir = self.artifact_dir();
        let artifact = self.controller.minimize(input_pair, &artifact_dir).await?;
        if artifact.status != zx::Status::OK {
            return Ok(artifact.status);
        }
        if let Some(path) = artifact.path {
            self.writer.println(format!("Minimized input written to '{}'", path.to_string_lossy()));
        };
        Ok(zx::Status::OK)
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
        let artifact = self.controller.merge().await?;
        if artifact.status == zx::Status::OK {
            self.fetch(fuzz::Corpus::Live).await?;
        }
        Ok(artifact.status)
    }

    /// Returns information about fuzzer execution.
    ///
    /// The status typically includes information such as how long the fuzzer has been running, how
    /// many edges in the call graph have been covered, how large the corpus is, etc.
    ///
    /// Refer to `fuchsia.fuzzer.Status` for precise details on the returned information.
    ///
    pub async fn status(&self) -> Result<fuzz::Status> {
        self.controller.get_status().await
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
        let corpus_stats = self.controller.read_corpus(corpus_type, corpus_dir).await?;
        let units = match corpus_stats.num_inputs {
            1 => "input",
            _ => "inputs",
        };
        let corpus_name = get_corpus_name(corpus_type);
        self.writer.println(format!(
            "Retrieved {} {} totaling {} bytes from the {} corpus.",
            corpus_stats.num_inputs, units, corpus_stats.total_size, corpus_name
        ));
        Ok(())
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
        self.writer.println("Configuring fuzzer...");
        self.controller.configure(fuzz_options).await
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
mod tests {
    use {
        super::{get_run_result, get_try_result, Fuzzer},
        crate::options,
        anyhow::{Context as _, Result},
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_fuzzer::{self as fuzz, Result_ as FuzzResult},
        fuchsia_async as fasync,
        fuchsia_fuzzctl::digest_path,
        fuchsia_fuzzctl_test::{
            add_defaults, create_task, serve_controller, verify_saved, BufferSink, FakeController,
            Test, TEST_URL,
        },
        fuchsia_zircon_status as zx,
        url::Url,
    };

    // Creates a test setup suitable for unit testing `Fuzzer`.
    fn perform_test_setup(
        test: &Test,
    ) -> Result<(FakeController, Fuzzer<BufferSink>, fasync::Task<()>)> {
        let url = Url::parse(TEST_URL)?;
        let (proxy, stream) = create_proxy_and_stream::<fuzz::ControllerMarker>()
            .context("failed to create FIDL connection")?;
        let fake = FakeController::new();
        let writer = test.writer();
        let fuzzer = Fuzzer::new(&url, proxy, test.root_dir(), &writer);
        let task = create_task(serve_controller(stream, fake.clone()), test.writer());
        Ok((fake, fuzzer, task))
    }

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
            fake: &FakeController,
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
            fake: &FakeController,
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
                    let artifact = digest_path(fuzzer.artifact_dir(), Some(result), input_data);
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
        test.verify_output()?;

        // Simulate a hung fuzzer.
        fake.set_result(Err(zx::Status::SHOULD_WAIT));
        let result = fuzzer.run(None, Some("100ms".to_string())).await;
        assert!(result.is_err());
        let msg = format!("{}", result.unwrap_err());
        assert!(msg.contains("workflow timed out"));
        Ok(())
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
        let artifact = digest_path(fuzzer.artifact_dir(), Some(FuzzResult::Cleansed), b"world");
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
        let artifact = digest_path(fuzzer.artifact_dir(), Some(FuzzResult::Minimized), b"world");
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
        let artifact = digest_path(fuzzer.artifact_dir(), Some(FuzzResult::Minimized), b"world");
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
