// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! test_list_tool generates test-list.json.

use {
    anyhow::Error,
    fidl::encoding::decode_persistent,
    fidl_fuchsia_component_decl::Component,
    fidl_fuchsia_data as fdata, fuchsia_archive, fuchsia_pkg,
    fuchsia_url::pkg_url::PkgUrl,
    serde::{Deserialize, Serialize},
    serde_json,
    std::{
        cmp::{Eq, PartialEq},
        fmt::Debug,
        fs,
        io::Read,
        path::PathBuf,
    },
    structopt::StructOpt,
    test_list::{TestList, TestListEntry, TestTag},
};

const META_FAR_PREFIX: &'static str = "meta/";
const TEST_TYPE_FACET: &'static str = "fuchsia.test.type";
const HERMETIC_TEST_TYPE: &'static str = "hermetic";
const TEST_TYPE_TAG_KEY: &'static str = "type";
const HERMETIC_TAG_KEY: &'static str = "hermetic";

mod error;
mod opts;

#[derive(Debug, Eq, PartialEq, Serialize, Deserialize)]
struct TestsJsonEntry {
    test: TestEntry,
}

#[derive(Debug, Eq, PartialEq, Serialize, Deserialize)]
struct TestEntry {
    name: String,
    label: String,
    cpu: String,
    os: String,
    package_url: Option<String>,
    package_manifests: Option<Vec<String>>,
}

fn find_meta_far(build_dir: &PathBuf, manifest_path: String) -> Result<PathBuf, Error> {
    let mut buffer = String::new();
    fs::File::open(build_dir.join(&manifest_path))?.read_to_string(&mut buffer)?;
    let package_manifest: fuchsia_pkg::PackageManifest = serde_json::from_str(&buffer)?;

    for blob in package_manifest.blobs() {
        if blob.path.eq(META_FAR_PREFIX) {
            return Ok(build_dir.join(&blob.source_path));
        }
    }
    Err(error::TestListToolError::MissingMetaBlob(manifest_path).into())
}

fn cm_decl_from_meta_far(meta_far_path: &PathBuf, cm_path: &str) -> Result<Component, Error> {
    let mut meta_far = fs::File::open(meta_far_path)?;
    let mut far_reader = fuchsia_archive::Reader::new(&mut meta_far)?;
    let cm_contents = far_reader.read_file(cm_path)?;
    let decl: Component = decode_persistent(&cm_contents)?;
    Ok(decl)
}

fn tags_from_facets(facets: &fdata::Dictionary) -> Result<Vec<TestTag>, Error> {
    for facet in facets.entries.as_ref().unwrap_or(&vec![]) {
        // TODO(rudymathu): CFv1 tests should not have a hermetic tag.
        if facet.key.eq(TEST_TYPE_FACET) {
            let val = facet
                .value
                .as_ref()
                .ok_or(error::TestListToolError::NullFacet(facet.key.clone()))?;
            match &**val {
                fdata::DictionaryValue::Str(s) => {
                    return Ok(vec![
                        TestTag { key: TEST_TYPE_TAG_KEY.to_string(), value: s.to_string() },
                        TestTag {
                            key: HERMETIC_TAG_KEY.to_string(),
                            value: s.eq(HERMETIC_TEST_TYPE).to_string(),
                        },
                    ]);
                }
                _ => {
                    return Err(error::TestListToolError::InvalidFacetValue(
                        facet.key.clone(),
                        format!("{:?}", val),
                    )
                    .into());
                }
            }
        }
    }
    Ok(vec![
        TestTag { key: TEST_TYPE_TAG_KEY.to_string(), value: HERMETIC_TEST_TYPE.to_string() },
        TestTag { key: HERMETIC_TAG_KEY.to_string(), value: "true".to_string() },
    ])
}

fn to_test_list_entry(test_entry: &TestEntry) -> TestListEntry {
    TestListEntry {
        name: test_entry.name.clone(),
        labels: vec![test_entry.label.clone()],
        tags: vec![
            TestTag { key: "cpu".to_string(), value: test_entry.cpu.clone() },
            TestTag { key: "os".to_string(), value: test_entry.os.clone() },
        ],
    }
}

fn main() -> Result<(), Error> {
    run_tool()
}

fn read_tests_json(file: &PathBuf) -> Result<Vec<TestsJsonEntry>, Error> {
    let mut buffer = String::new();
    fs::File::open(&file)?.read_to_string(&mut buffer)?;
    let t: Vec<TestsJsonEntry> = serde_json::from_str(&buffer)?;
    Ok(t)
}

fn tags_from_manifest(package_url: String, meta_far_path: &PathBuf) -> Result<Vec<TestTag>, Error> {
    let pkg_url = PkgUrl::parse(&package_url)?;
    let cm_path =
        pkg_url.resource().ok_or(error::TestListToolError::InvalidPackageURL(package_url))?;
    let decl = cm_decl_from_meta_far(&meta_far_path, cm_path)?;
    tags_from_facets(&decl.facets.unwrap_or(fdata::Dictionary::EMPTY))
}

fn write_depfile(depfile: &PathBuf, output: &PathBuf, inputs: &Vec<PathBuf>) -> Result<(), Error> {
    if inputs.len() == 0 {
        return Ok(());
    }
    let contents = format!(
        "{}: {}\n",
        output.display(),
        &inputs.iter().map(|i| format!(" {}", i.display())).collect::<String>(),
    );
    fs::write(depfile, contents)?;
    Ok(())
}

fn run_tool() -> Result<(), Error> {
    let opt = opts::Opt::from_args();
    opt.validate()?;

    let tests_json = read_tests_json(&opt.input)?;
    let mut test_list = TestList { tests: vec![] };
    let mut inputs: Vec<PathBuf> = vec![];

    for entry in tests_json {
        // Construct the base TestListEntry.
        let mut test_list_entry = to_test_list_entry(&entry.test);
        let pkg_manifests = entry.test.package_manifests.unwrap_or(vec![]);

        // Aggregate any tags from the component manifest of the test.
        if entry.test.package_url.is_some() && pkg_manifests.len() > 0 {
            let pkg_url = entry.test.package_url.unwrap();
            let pkg_manifest = pkg_manifests[0].clone();
            inputs.push(pkg_manifest.clone().into());

            let res = find_meta_far(&opt.build_dir, pkg_manifest.clone());
            if res.is_err() {
                println!(
                    "error finding meta.far file in package manifest {}: {:?}",
                    &pkg_manifest,
                    res.unwrap_err()
                );
                continue;
            }
            let meta_far_path = res.unwrap();
            inputs.push(meta_far_path.clone());

            match tags_from_manifest(pkg_url.clone(), &meta_far_path) {
                Ok(mut tags) => test_list_entry.tags.append(&mut tags),
                Err(e) => {
                    println!("error processing manifest for package URL {}: {:?}", &pkg_url, e)
                }
            }
        }
        test_list.tests.push(test_list_entry);
    }
    let test_list_json = serde_json::to_string_pretty(&test_list)?;
    fs::write(&opt.output, test_list_json)?;
    if let Some(depfile) = opt.depfile {
        write_depfile(&depfile, &opt.output, &inputs)?;
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use {super::*, tempfile::tempdir};

    #[test]
    fn test_find_meta_far() {
        let build_dir = tempdir().expect("failed to get tempdir");
        let package_manifest_path = "package_manifest.json";

        // Test the working case.
        let mut contents = r#"
            {
                "version": "1",
                "repository": "fuchsia.com",
                "package": {
                    "name": "echo-integration-test",
                    "version": "0"
                },
                "blobs": [
                    {
                        "source_path": "obj/build/components/tests/echo-integration-test/meta.far",
                        "path": "meta/",
                        "merkle": "0ec72cdf55fec3e0cc3dd47e86b95ee62c974ebaebea1d05769fea3fc4edca0b",
                        "size": 36864
                    }
                ]
            }"#;
        fs::write(build_dir.path().join(package_manifest_path), contents)
            .expect("failed to write fake package manifest");
        assert_eq!(
            find_meta_far(&build_dir.path().to_path_buf(), package_manifest_path.into()).unwrap(),
            build_dir.path().join("obj/build/components/tests/echo-integration-test/meta.far"),
        );

        // Test the error case.
        contents = r#"
            {
                "version": "1",
                "repository": "fuchsia.com",
                "package": {
                    "name": "echo-integration-test",
                    "version": "0"
                },
                "blobs": []
            }"#;
        fs::write(build_dir.path().join(package_manifest_path), contents)
            .expect("failed to write fake package manifest");
        let err = find_meta_far(&build_dir.path().to_path_buf(), package_manifest_path.into())
            .expect_err("find_meta_far failed unexpectedly");
        match err.downcast_ref::<error::TestListToolError>() {
            Some(error::TestListToolError::MissingMetaBlob(path)) => {
                assert_eq!(package_manifest_path.to_string(), *path)
            }
            Some(e) => panic!("find_meta_far returned incorrect TestListToolError: {:?}", e),
            None => panic!("find_meta_far returned non TestListToolError: {:?}", err),
        }
    }

    #[test]
    fn test_tags_from_facets() {
        // Test that empty facets return the hermetic tags.
        let mut facets = fdata::Dictionary::EMPTY;
        let mut tags = tags_from_facets(&facets).expect("failed get tags in tags_from_facets");
        let hermetic_tags = vec![
            TestTag { key: TEST_TYPE_TAG_KEY.to_string(), value: HERMETIC_TEST_TYPE.to_string() },
            TestTag { key: HERMETIC_TAG_KEY.to_string(), value: "true".to_string() },
        ];
        assert_eq!(tags, hermetic_tags);

        // Test that a facet of fuchsia.test: tests returns hermetic tags.
        facets.entries = Some(vec![fdata::DictionaryEntry {
            key: TEST_TYPE_FACET.to_string(),
            value: Some(Box::new(fdata::DictionaryValue::Str(HERMETIC_TEST_TYPE.to_string()))),
        }]);
        tags = tags_from_facets(&facets).expect("failed get tags in tags_from_facets");
        assert_eq!(tags, hermetic_tags);

        // Test that a null fuchsia.test facet returns a NullFacet error.
        facets.entries =
            Some(vec![fdata::DictionaryEntry { key: TEST_TYPE_FACET.to_string(), value: None }]);
        let err =
            tags_from_facets(&facets).expect_err("tags_from_facets succeeded on null facet value");
        match err.downcast_ref::<error::TestListToolError>() {
            Some(error::TestListToolError::NullFacet(key)) => {
                assert_eq!(*key, TEST_TYPE_FACET.to_string());
            }
            Some(e) => panic!("tags_from_facets returned incorrect TestListToolError: {:?}", e),
            None => panic!("tags_from_facets returned non-TestListToolError: {:?}", err),
        }

        // Test that an invalid fuchsia.test facet returns an InvalidFacetValue error.
        facets.entries = Some(vec![fdata::DictionaryEntry {
            key: TEST_TYPE_FACET.to_string(),
            value: Some(Box::new(fdata::DictionaryValue::StrVec(vec![
                HERMETIC_TEST_TYPE.to_string()
            ]))),
        }]);
        let err =
            tags_from_facets(&facets).expect_err("tags_from_facets succeeded on null facet value");
        match err.downcast_ref::<error::TestListToolError>() {
            Some(error::TestListToolError::InvalidFacetValue(k, _)) => {
                assert_eq!(*k, TEST_TYPE_FACET.to_string());
            }
            Some(e) => panic!("tags_from_facets returned incorrect TestListToolError: {:?}", e),
            None => panic!("tags_from_facets returned non-TestListToolError: {:?}", err),
        }
    }

    #[test]
    fn test_to_test_list_entry() {
        let test_entry = TestEntry {
            name: "test-name".to_string(),
            label: "test-label".to_string(),
            cpu: "x64".to_string(),
            os: "linux".to_string(),
            package_url: Some(
                "fuchsia-pkg://fuchsia.com/echo-integration-test#meta/echo-client-test.cm"
                    .to_string(),
            ),
            package_manifests: Some(vec![
                "obj/build/components/tests/echo-integration-test/package_manifest.json"
                    .to_string(),
            ]),
        };
        let test_list_entry = to_test_list_entry(&test_entry);
        assert_eq!(
            test_list_entry,
            TestListEntry {
                name: "test-name".to_string(),
                labels: vec!["test-label".to_string()],
                tags: vec![
                    TestTag { key: "cpu".to_string(), value: "x64".to_string() },
                    TestTag { key: "os".to_string(), value: "linux".to_string() },
                ],
            }
        )
    }

    #[test]
    fn test_read_tests_json() {
        let data = r#"
            [
                {
                    "test": {
                        "cpu": "x64",
                        "label": "//build/components/tests:echo-integration-test_test_echo-client-test(//build/toolchain/fuchsia:x64)",
                        "log_settings": {
                            "max_severity": "WARN"
                        },
                        "name": "fuchsia-pkg://fuchsia.com/echo-integration-test#meta/echo-client-test.cm",
                        "os": "fuchsia",
                        "package_label": "//build/components/tests:echo-integration-test(//build/toolchain/fuchsia:x64)",
                        "package_manifests": [
                            "obj/build/components/tests/echo-integration-test/package_manifest.json"
                        ],
                        "package_url": "fuchsia-pkg://fuchsia.com/echo-integration-test#meta/echo-client-test.cm"
                    }
                }
            ]"#;
        let dir = tempdir().expect("failed to get tempdir");
        let tests_json_path = dir.path().join("tests.json");
        fs::write(&tests_json_path, data).expect("failed to write tests.json to tempfile");
        let tests_json = read_tests_json(&tests_json_path).expect("read_tests_json() failed");
        assert_eq!(
            tests_json,
            vec![
                TestsJsonEntry{
                    test: TestEntry{
                        name: "fuchsia-pkg://fuchsia.com/echo-integration-test#meta/echo-client-test.cm".to_string(),
                        label: "//build/components/tests:echo-integration-test_test_echo-client-test(//build/toolchain/fuchsia:x64)".to_string(),
                        cpu: "x64".to_string(),
                        os: "fuchsia".to_string(),
                        package_url: Some("fuchsia-pkg://fuchsia.com/echo-integration-test#meta/echo-client-test.cm".to_string()),
                        package_manifests: Some(vec![
                            "obj/build/components/tests/echo-integration-test/package_manifest.json".to_string(),
                        ]),
                    },
                }
            ],
        );
    }
}
