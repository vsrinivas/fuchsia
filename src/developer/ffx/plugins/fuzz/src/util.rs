// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains a few helper routines that don't fit nicely anywhere else.

use {
    anyhow::{bail, Context as _, Result},
    fidl_fuchsia_fuzzer as fuzz,
    serde_json::Value,
    sha2::{Digest, Sha256},
    std::fs,
    std::path::{Path, PathBuf},
    url::Url,
};

/// Creates a directory under the given `parent` directory, if it does not already exist.
pub fn create_dir_at<P: AsRef<Path>, S: AsRef<str>>(parent: P, dirname: S) -> Result<PathBuf> {
    let mut pathbuf = PathBuf::from(parent.as_ref());
    pathbuf.push(dirname.as_ref());
    fs::create_dir_all(&pathbuf)
        .with_context(|| format!("failed to create directory: '{}'", pathbuf.to_string_lossy()))?;
    Ok(pathbuf)
}

/// Returns the path under `output_dir` where a fuzzer could store artifacts.
pub fn create_artifact_dir<P: AsRef<Path>>(output_dir: P) -> Result<PathBuf> {
    create_dir_at(output_dir, "artifacts")
}

/// Returns the path under `output_dir` where a fuzzer could store a corpus of the given
/// |corpus_type|.
pub fn create_corpus_dir<P: AsRef<Path>>(
    output_dir: P,
    corpus_type: fuzz::Corpus,
) -> Result<PathBuf> {
    match corpus_type {
        fuzz::Corpus::Seed => create_dir_at(output_dir, "seed-corpus"),
        fuzz::Corpus::Live => create_dir_at(output_dir, "corpus"),
        other => unreachable!("unsupported type: {:?}", other),
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
mod tests {
    use {super::get_fuzzer_urls, crate::test_fixtures::Test, anyhow::Result, serde_json::json};

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
