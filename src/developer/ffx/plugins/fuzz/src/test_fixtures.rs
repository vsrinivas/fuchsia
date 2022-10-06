// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains test fixtures for emulating a development host with a source tree and a target
// device that includes the remote control service and fuzz-manager components.
//
// Additional test fixtures that are related to other `ffx fuzz` types are co-located with those
// types, e.g. `FakeFuzzer` in fuzzer.rs and `serve_manager` in manager.rs.

use {
    crate::fuzzer::test_fixtures::FakeFuzzer,
    crate::manager::test_fixtures::serve_manager,
    crate::util::{create_artifact_dir, create_corpus_dir},
    crate::writer::test_fixtures::BufferSink,
    crate::writer::{OutputSink, Writer},
    anyhow::{anyhow, bail, Context as _, Result},
    fidl::endpoints::{create_proxy, ServerEnd},
    fidl_fuchsia_developer_remotecontrol as rcs, fidl_fuchsia_fuzzer as fuzz,
    fuchsia_async as fasync,
    futures::{Future, StreamExt},
    serde_json::json,
    std::cell::RefCell,
    std::env,
    std::fmt::Debug,
    std::fmt::Display,
    std::fs,
    std::path::{Path, PathBuf},
    std::rc::Rc,
    tempfile::{tempdir, TempDir},
};

pub const TEST_URL: &str = "fuchsia-pkg://fuchsia.com/fake#meta/foo-fuzzer.cm";

/// General purpose test context for the `ffx fuzz` plugin unit tests.
///
/// This object groups several commonly used routines used to capture data produced as a part of
/// unit tests, such as:
///
///   * It can create temporary files and directories, and ensure they persist for duration of
///     the test.
///   * It maintains a list of expected outputs.
///   * It shares a list of actual outputs with a `BufferSink` and can verify that they match
///     its expectations.
///   * It can produce `Writer`s backed by its associated `BufferSink`.
///
#[derive(Debug)]
pub struct Test {
    // This temporary directory is used indirectly via `root_dir`, but must be kept in scope for
    // the duration of the test to avoid it being deleted prematurely.
    _tmp_dir: Option<TempDir>,
    root_dir: PathBuf,
    task: Option<fasync::Task<()>>,
    fuzzer: FakeFuzzer,
    expected: Vec<Expectation>,
    actual: Rc<RefCell<Vec<u8>>>,
    writer: Writer<BufferSink>,
}

// Output can be tested for exact matches or substring matches.
#[derive(Clone, Debug)]
pub enum Expectation {
    Equals(String),
    Contains(String),
}

impl Test {
    /// Creates a new `Test`.
    ///
    /// When running tests, users may optionally set the FFX_FUZZ_ECHO_TEST_OUTPUT environment
    /// variable, which will cause this object to use an existing directory rather than create a
    /// temporary one.
    pub fn try_new() -> Result<Self> {
        let (tmp_dir, root_dir) = match env::var("FFX_FUZZ_TEST_ROOT_DIR") {
            Ok(root_dir) => (None, PathBuf::from(root_dir)),
            Err(_) => {
                let tmp_dir = tempdir().context("failed to create test directory")?;
                let root_dir = PathBuf::from(tmp_dir.path());
                (Some(tmp_dir), root_dir)
            }
        };
        let actual = Rc::new(RefCell::new(Vec::new()));
        let mut writer = Writer::new(BufferSink::new(Rc::clone(&actual)));
        writer.use_colors(false);
        Ok(Self {
            _tmp_dir: tmp_dir,
            root_dir,
            task: None,
            fuzzer: FakeFuzzer::new(),
            expected: Vec::new(),
            actual,
            writer,
        })
    }

    /// Returns the writable temporary directory for this test.
    pub fn root_dir(&self) -> &Path {
        self.root_dir.as_path()
    }

    /// Creates a directory under this object's `root_dir`.
    ///
    /// The given `path` may be relative or absolute, but if it is the latter it must be
    /// prefixed with this object's `root_dir`.
    ///
    /// Returns a `PathBuf` on success and an error if the `path` is outside the `root_dir` or
    /// the filesystem returns an error.
    ///
    pub fn create_dir<P: AsRef<Path>>(&self, path: P) -> Result<PathBuf> {
        let path = path.as_ref();
        let mut abspath = PathBuf::from(self.root_dir());
        if path.is_relative() {
            abspath.push(path);
        } else if path.starts_with(self.root_dir()) {
            abspath = PathBuf::from(path);
        } else {
            bail!(
                "cannot create test directories outside the test root: {}",
                path.to_string_lossy()
            );
        }
        fs::create_dir_all(&abspath).with_context(|| {
            format!("failed to create '{}' directory", abspath.to_string_lossy())
        })?;
        Ok(abspath)
    }

    /// Returns the path where the fuzzer will store artifacts.
    pub fn artifact_dir(&self) -> PathBuf {
        create_artifact_dir(&self.root_dir).unwrap()
    }

    /// Returns the path where the fuzzer will store its corpus of the given type.
    pub fn corpus_dir(&self, corpus_type: fuzz::Corpus) -> PathBuf {
        create_corpus_dir(&self.root_dir, corpus_type).unwrap()
    }

    /// Creates a fake ".fx-build-dir" file for testing.
    ///
    /// The ".fx-build-dir" file will be created under this object's `root_dir`, and will
    /// contain the `relative_path` to the build directory. Except for some `util::tests`, unit
    /// tests should prefer `create_tests_json`.
    ///
    /// Returns an error if any filesystem operations fail.
    ///
    pub fn write_fx_build_dir<P: AsRef<Path>>(&self, build_dir: P) -> Result<()> {
        let build_dir = build_dir.as_ref();
        let mut fx_build_dir = PathBuf::from(self.root_dir());
        fx_build_dir.push(".fx-build-dir");
        let build_dir = build_dir.to_string_lossy().to_string();
        fs::write(&fx_build_dir, &build_dir)
            .with_context(|| format!("failed to write to '{}'", fx_build_dir.to_string_lossy()))?;
        Ok(())
    }

    /// Creates a fake "tests.json" file for testing.
    ///
    /// The "tests.json" will be created under the `relative_path` from this object's `root_dir`
    /// and will contain the given `contents`. Except for some `util::tests`, unit tests should
    /// prefer `create_tests_json`.
    ///
    /// Returns an error if any filesystem operations fail.
    ///
    pub fn write_tests_json<P: AsRef<Path>, S: AsRef<str>>(
        &self,
        build_dir: P,
        contents: S,
    ) -> Result<PathBuf> {
        let build_dir = build_dir.as_ref();
        let mut tests_json = PathBuf::from(build_dir);
        tests_json.push("tests.json");
        fs::write(&tests_json, contents.as_ref())
            .with_context(|| format!("failed to write to '{}'", tests_json.to_string_lossy()))?;
        Ok(tests_json)
    }

    /// Creates a fake "tests.json" file for testing.
    ///
    /// The "tests.json" will include an array of valid JSON objects for the given `urls`.
    ///
    /// Returns an error if any filesystem operations fail.
    ///
    pub fn create_tests_json<D: Display>(&self, urls: impl Iterator<Item = D>) -> Result<PathBuf> {
        let build_dir = self
            .create_dir("out/default")
            .context("failed to create build directory for 'tests.json'")?;
        self.write_fx_build_dir(&build_dir).with_context(|| {
            format!("failed to set build directory to '{}'", build_dir.to_string_lossy())
        })?;

        let json_data: Vec<_> = urls
            .map(|url| {
                json!({
                    "test": {
                        "build_rule": "fuchsia_fuzzer_package",
                        "package_url": url.to_string()
                    }
                })
            })
            .collect();
        let json_data = json!(json_data);
        self.write_tests_json(&build_dir, json_data.to_string()).with_context(|| {
            format!("failed to create '{}/tests.json'", build_dir.to_string_lossy())
        })
    }

    /// Creates several temporary `files` from the given iterator for testing.
    ///
    /// Each file's contents will simpl be its name, which is taken from the given `files`.
    ///
    /// Returns an error if writing to the filesystem fails.
    ///
    pub fn create_test_files<P: AsRef<Path>, D: Display>(
        &self,
        test_dir: P,
        files: impl Iterator<Item = D>,
    ) -> Result<()> {
        let test_dir = test_dir.as_ref();
        for filename in files {
            let filename = filename.to_string();
            fs::write(test_dir.join(&filename), filename.as_bytes())
                .with_context(|| format!("failed to write to '{}'", filename))?;
        }
        Ok(())
    }

    /// Returns a proxy to the remote control service.
    pub fn rcs(&mut self) -> Result<rcs::RemoteControlProxy> {
        let (proxy, server_end) = create_proxy::<rcs::RemoteControlMarker>()?;
        self.task = Some(create_task(
            serve_rcs(server_end, self.fuzzer.clone(), self.writer.clone()),
            self.writer.clone(),
        ));
        Ok(proxy)
    }

    /// Clones the fake fuzzer "connected" by the fake manager.
    pub fn fuzzer(&self) -> FakeFuzzer {
        self.fuzzer.clone()
    }

    /// Adds an expectation that an output written to the `BufferSink` will exactly match `msg`.
    pub fn output_matches<T: AsRef<str> + Display>(&mut self, msg: T) {
        let msg = msg.as_ref().trim().to_string();
        if !msg.is_empty() {
            self.expected.push(Expectation::Equals(msg));
        }
    }

    /// Adds an expectation that an output written to the `BufferSink` will contain `msg`.
    pub fn output_includes<T: AsRef<str> + Display>(&mut self, msg: T) {
        let msg = msg.as_ref().trim().to_string();
        if !msg.is_empty() {
            self.expected.push(Expectation::Contains(msg));
        }
    }

    /// Iterates over the expected and actual output and verifies expectations are met.
    pub fn verify_output(&mut self) -> Result<()> {
        let actual: Vec<u8> = {
            let mut actual = self.actual.borrow_mut();
            actual.drain(..).collect()
        };
        let actual = String::from_utf8_lossy(&actual);
        let mut actual: Vec<String> = actual.split("\n").map(|s| s.trim().to_string()).collect();
        actual.retain(|s| !s.is_empty());
        let mut actual = actual.into_iter();

        // `Contains` expectations may be surrounded by extra lines.
        let mut extra = false;
        for expectation in self.expected.drain(..) {
            loop {
                let line = actual.next().ok_or(anyhow!("unmet expectation: {:?}", expectation))?;
                match &expectation {
                    Expectation::Equals(msg) if line == *msg => {
                        extra = false;
                        break;
                    }
                    Expectation::Equals(_msg) if extra => continue,
                    Expectation::Equals(msg) => {
                        bail!("mismatch:\n  actual=`{}`\nexpected=`{}`", line, msg)
                    }
                    Expectation::Contains(msg) => {
                        extra = true;
                        if line.contains(msg) {
                            break;
                        }
                    }
                }
            }
        }
        if !extra {
            if let Some(line) = actual.next() {
                bail!("unexpected line: {}", line);
            }
        }
        Ok(())
    }

    /// Returns a `Writer` using the `BufferSink` associated with this object.
    pub fn writer(&self) -> &Writer<BufferSink> {
        &self.writer
    }
}

/// Starts serving the fake remote control service for this test.
pub async fn serve_rcs(
    server_end: ServerEnd<rcs::RemoteControlMarker>,
    fuzzer: FakeFuzzer,
    writer: Writer<BufferSink>,
) -> Result<()> {
    let mut stream = server_end.into_stream()?;
    let mut task = None;
    while let Some(request) = stream.next().await {
        match request {
            Ok(rcs::RemoteControlRequest::Connect { selector: _, service_chan, responder }) => {
                let server_end = ServerEnd::<fuzz::ManagerMarker>::new(service_chan);
                let mut response = Ok(rcs::ServiceMatch {
                    moniker: vec!["moniker".to_string()],
                    subdir: "subdir".to_string(),
                    service: "service".to_string(),
                });
                responder.send(&mut response)?;
                task = Some(create_task(
                    serve_manager(server_end, fuzzer.clone(), writer.clone()),
                    writer.clone(),
                ));
            }
            Err(e) => return Err(anyhow!(e)),
            _ => todo!("not yet implemented"),
        }
    }
    if let Some(task) = task.take() {
        task.await;
    }
    Ok(())
}

/// Wraps a given `future` to display any returned errors using the given `writer`.
pub fn create_task<F, O>(future: F, writer: Writer<O>) -> fasync::Task<()>
where
    F: Future<Output = Result<()>> + 'static,
    O: OutputSink,
{
    let wrapped = || async move {
        if let Err(e) = future.await {
            writer.error(format!("task failed: {:?}", e));
        }
    };
    fasync::Task::local(wrapped())
}
