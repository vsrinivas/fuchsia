// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains a few helper routines that don't fit nicely anywhere else.

use {
    anyhow::{bail, Context as _, Result},
    serde_json::Value,
    sha2::{Digest, Sha256},
    std::fs,
    std::path::{Path, PathBuf},
    url::Url,
};

/// Converts a string to a PathBuf and verifies it is writable.
///
/// If `path` is Some, verifies it is an existing directory that can be written to and converts it
/// to Some(PathBuf); otherwise, returns None.
pub fn to_out_dir<S: AsRef<str>>(path: Option<S>) -> Result<Option<PathBuf>> {
    match path {
        Some(path) => {
            let path = PathBuf::from(path.as_ref());
            let metadata = fs::metadata(&path).context("failed to get metadata")?;
            if !metadata.is_dir() {
                bail!("not a directory");
            }
            if metadata.permissions().readonly() {
                bail!("read-only");
            }
            Ok(Some(path))
        }
        None => Ok(None),
    }
}
/// Generates the path for a file based on its contents.
///
/// Returns a `PathBuf` for a file in the `out_dir` that is named by the concatenating the `prefix`,
/// if provided, with the hex encoded SHA-256 digest of the `data`. This naming scheme is used both
/// for inputs retrieved from a fuzzer corpus and for artifacts produced by the fuzzer.
///
pub fn digest_path<P: AsRef<Path>>(out_dir: P, prefix: Option<&str>, data: &[u8]) -> PathBuf {
    let mut path = PathBuf::from(out_dir.as_ref());
    let mut digest = Sha256::new();
    digest.update(&data);
    match prefix {
        Some(prefix) => path.push(format!("{}-{:x}", prefix, digest.finalize())),
        None => path.push(format!("{:x}", digest.finalize())),
    };
    path
}

/// Gets URLs for available fuzzers.
///
/// Reads from the filesystem and parses the build metadata to produce a list of URLs for fuzzer
/// packages.
pub fn get_fuzzer_urls<P: AsRef<Path>>(fuchsia_dir: P) -> Result<Vec<Url>> {
    // Find tests.json.
    let mut fx_build_dir = PathBuf::from(fuchsia_dir.as_ref());
    fx_build_dir.push(".fx-build-dir");
    let mut fx_build_dir = fs::read_to_string(&fx_build_dir)
        .with_context(|| format!("failed to read '{}'", fx_build_dir.to_string_lossy()))?;

    fx_build_dir.retain(|c| !c.is_whitespace());
    let mut tests_json = PathBuf::from(fuchsia_dir.as_ref());
    tests_json.push(&fx_build_dir);
    tests_json.push("tests.json");

    // Extract fuzzers.
    let json_data = fs::read_to_string(&tests_json)
        .with_context(|| format!("failed to read '{}'", tests_json.to_string_lossy()))?;
    parse_tests_json(json_data)
        .with_context(|| format!("failed to parse '{}'", tests_json.to_string_lossy()))
}

fn parse_tests_json(json_data: String) -> Result<Vec<Url>> {
    let deserialized = serde_json::from_str(&json_data).context("failed to deserialize")?;
    let tests = match deserialized {
        Value::Array(tests) => tests,
        _ => bail!("root object is not array"),
    };
    let mut fuzzer_urls = Vec::new();
    for test in tests {
        let metadata = match test.get("test") {
            Some(Value::Object(metadata)) => metadata,
            Some(_) => bail!("found 'test' field that is not an object"),
            None => continue,
        };
        let build_rule = match metadata.get("build_rule") {
            Some(Value::String(build_rule)) => build_rule,
            Some(_) => bail!("found 'build_rule' field that is not a string"),
            None => continue,
        };
        if build_rule != "fuchsia_fuzzer_package" {
            continue;
        }
        let package_url = match metadata.get("package_url") {
            Some(Value::String(package_url)) => package_url,
            Some(_) => bail!("found 'package_url' field that is not a string"),
            None => continue,
        };
        let url = Url::parse(package_url).context("failed to parse URL")?;
        fuzzer_urls.push(url);
    }
    Ok(fuzzer_urls)
}

#[cfg(test)]
pub mod test_fixtures {
    use {
        crate::writer::test_fixtures::BufferSink,
        crate::writer::{OutputSink, Writer},
        anyhow::{anyhow, bail, Context as _, Result},
        fuchsia_async as fasync,
        futures::Future,
        serde_json::json,
        std::cell::RefCell,
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
        root_dir: TempDir,
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
        pub fn try_new() -> Result<Self> {
            let root_dir = tempdir().context("failed to create test directory")?;
            let expected = Vec::new();
            let actual = Rc::new(RefCell::new(Vec::new()));
            let mut writer = Writer::new(BufferSink::new(Rc::clone(&actual)));
            writer.use_colors(false);
            Ok(Self { root_dir, expected, actual, writer })
        }

        /// Returns the writable temporary directory for this test.
        pub fn root_dir(&self) -> &Path {
            self.root_dir.path()
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
            fs::write(&fx_build_dir, &build_dir).with_context(|| {
                format!("failed to write to '{}'", fx_build_dir.to_string_lossy())
            })?;
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
        ) -> Result<()> {
            let build_dir = build_dir.as_ref();
            let mut tests_json = PathBuf::from(build_dir);
            tests_json.push("tests.json");
            fs::write(&tests_json, contents.as_ref()).with_context(|| {
                format!("failed to write to '{}'", tests_json.to_string_lossy())
            })?;
            Ok(())
        }

        /// Creates a fake "tests.json" file for testing.
        ///
        /// The "tests.json" will include an array of valid JSON objects for the given `urls`.
        ///
        /// Returns an error if any filesystem operations fail.
        ///
        pub fn create_tests_json<D: Display>(&self, urls: impl Iterator<Item = D>) -> Result<()> {
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
            })?;
            Ok(())
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
            let mut actual: Vec<String> =
                actual.split("\n").map(|s| s.trim().to_string()).collect();
            actual.retain(|s| !s.is_empty());
            let mut actual = actual.into_iter();

            // `Contains` expectations may be surrounded by extra lines.
            let mut extra = false;
            for expectation in self.expected.drain(..) {
                loop {
                    let line =
                        actual.next().ok_or(anyhow!("unmet expectation: {:?}", expectation))?;
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
}

#[cfg(test)]
mod tests {
    use {super::get_fuzzer_urls, super::test_fixtures::Test, anyhow::Result, serde_json::json};

    #[fuchsia::test]
    async fn test_get_fuzzer_urls() -> Result<()> {
        let test = Test::try_new()?;
        let build_dir = test.create_dir("out/default")?;

        // Missing .fx-build-dir
        let fuchsia_dir = test.root_dir();
        let actual = format!("{:?}", get_fuzzer_urls(&fuchsia_dir));
        let expected = format!("failed to read '{}/.fx-build-dir'", fuchsia_dir.to_string_lossy());
        assert!(actual.contains(&expected));

        // Missing tests.json
        test.write_fx_build_dir(&build_dir)?;
        let actual = format!("{:?}", get_fuzzer_urls(&fuchsia_dir));
        let expected = format!("failed to read '{}/tests.json'", build_dir.to_string_lossy());
        assert!(actual.contains(&expected));

        // tests.json is not JSON.
        test.write_tests_json(&build_dir, "hello world!\n")?;
        let actual = format!("{:?}", get_fuzzer_urls(&fuchsia_dir));
        assert!(actual.contains("expected value"));

        // tests.json is not an array.
        let json_data = json!({
            "foo": 1
        });
        test.write_tests_json(&build_dir, json_data.to_string())?;
        let actual = format!("{:?}", get_fuzzer_urls(&fuchsia_dir));
        assert!(actual.contains("root object is not array"));

        // tests.json contains empty array
        let json_data = json!([]);
        test.write_tests_json(&build_dir, json_data.to_string())?;
        let fuzzers = get_fuzzer_urls(&fuchsia_dir)?;
        assert!(fuzzers.is_empty());

        // Various malformed tests.jsons
        let json_data = json!([
            {
                "test": 1
            }
        ]);
        test.write_tests_json(&build_dir, json_data.to_string())?;
        let actual = format!("{:?}", get_fuzzer_urls(&fuchsia_dir));
        assert!(actual.contains("found 'test' field that is not an object"));

        let json_data = json!([
            {
                "test": {
                    "build_rule": 1
                }
            }
        ]);
        test.write_tests_json(&build_dir, json_data.to_string())?;
        let actual = format!("{:?}", get_fuzzer_urls(&fuchsia_dir));
        assert!(actual.contains("found 'build_rule' field that is not a string"));

        let json_data = json!([
            {
                "test": {
                    "build_rule": "fuchsia_fuzzer_package",
                    "package_url": 1
                }
            }
        ]);
        test.write_tests_json(&build_dir, json_data.to_string())?;
        let actual = format!("{:?}", get_fuzzer_urls(fuchsia_dir));
        assert!(actual.contains("found 'package_url' field that is not a string"));

        let json_data = json!([
            {
                "test": {
                    "build_rule": "fuchsia_fuzzer_package",
                    "package_url": "not a valid URL"
                }
            }
        ]);
        test.write_tests_json(&build_dir, json_data.to_string())?;
        let actual = format!("{:?}", get_fuzzer_urls(fuchsia_dir));
        assert!(actual.contains("failed to parse URL"));

        // tests.json contains fuzzers mixed with other tests.
        let json_data = json!([
            {
                "test": {
                    "name": "host-test"
                }
            },
            {
                "test": {
                    "build_rule": "fuchsia_fuzzer_package",
                    "package_url": "fuchsia-pkg://fuchsia.com/fake#meta/foo-fuzzer.cm"
                }
            },
            {
                "test": {
                    "build_rule": "fuchsia_test_package",
                    "package_url": "fuchsia-pkg://fuchsia.com/fake#meta/unittests.cm"
                }
            },
            {
                "test": {
                    "build_rule": "fuchsia_fuzzer_package",
                    "package_url": "fuchsia-pkg://fuchsia.com/fake#meta/bar-fuzzer.cm"
                }
            }
        ]);
        test.write_tests_json(&build_dir, json_data.to_string())?;
        let urls = get_fuzzer_urls(fuchsia_dir)?;
        assert_eq!(urls[0].as_str(), "fuchsia-pkg://fuchsia.com/fake#meta/foo-fuzzer.cm");
        assert_eq!(urls[1].as_str(), "fuchsia-pkg://fuchsia.com/fake#meta/bar-fuzzer.cm");
        Ok(())
    }
}
