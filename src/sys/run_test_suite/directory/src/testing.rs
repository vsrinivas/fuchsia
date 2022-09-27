// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{ArtifactMetadata, MaybeUnknown, Outcome, SuiteResult, TestCaseResult, TestRunResult};
use std::{
    collections::{HashMap, HashSet},
    ops::Deref,
    path::{Path, PathBuf},
};
use test_list::TestTag;

enum MatchOption<T> {
    AnyOrNone,
    None,
    Any,
    Specified(T),
}

macro_rules! assert_match_option {
    ($expected:expr, $actual:expr, $field:expr) => {
        match $expected {
            MatchOption::AnyOrNone => (),
            MatchOption::None => {
                assert_eq!(None, $actual, "Expected {} to be None but was {:?}", $field, $actual)
            }
            MatchOption::Any => {
                assert!($actual.is_some(), "Expected {} to contain a value but was None", $field)
            }
            MatchOption::Specified(val) => assert_eq!(
                Some(val),
                $actual,
                "Expected {} to be {:?} but was {:?}",
                $field,
                Some(val),
                $actual
            ),
        }
    };
}

/// Container that identifies the entity that is being verified in an assertion.
#[derive(Clone, Copy)]
enum EntityContext<'a> {
    Run,
    Suite(&'a ExpectedSuite),
    Case(&'a ExpectedSuite, &'a ExpectedTestCase),
}

/// Container that identifies the artifact that is being verified in an assertion.
#[derive(Clone, Copy)]
struct ArtifactContext<'a, 'b> {
    entity: &'a EntityContext<'b>,
    metadata: &'a ArtifactMetadata,
}

impl std::fmt::Display for EntityContext<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Run => write!(f, "TEST RUN"),
            Self::Suite(suite) => write!(f, "SUITE {}", suite.name),
            Self::Case(suite, case) => write!(f, "SUITE {}: CASE {}", suite.name, case.name),
        }
    }
}

impl std::fmt::Display for ArtifactContext<'_, '_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "Entity: {}, Metadata: {:?}", self.entity, self.metadata)
    }
}

/// A mapping from artifact metadata to assertions made on the artifact.
type ArtifactMetadataToAssertionMap = HashMap<ArtifactMetadata, ExpectedArtifact>;

/// Assert that the run results contained in `actual_run` and the directory specified by `root`
/// contain the results and artifacts in `expected_run`.
pub fn assert_run_result(root: &Path, expected_run: &ExpectedTestRun) {
    let context = EntityContext::Run;
    let actual_run = TestRunResult::from_dir(root).expect("Parse output directory");
    let TestRunResult { common, suites } = actual_run;
    assert_match_option!(
        expected_run.duration_milliseconds,
        common.deref().duration_milliseconds,
        format!("Run duration for {}", context)
    );
    assert_match_option!(
        expected_run.start_time,
        common.deref().start_time,
        format!("Start time for {}", context)
    );
    assert_eq!(common.deref().outcome, expected_run.outcome, "Outcome for {}", context);
    assert_artifacts(
        root,
        &common.deref().artifact_dir.root,
        &common.deref().artifact_dir.artifacts,
        &expected_run.artifacts,
        EntityContext::Run,
    );
    assert_suite_results(root, &suites, &expected_run.suites);
}

/// Assert that the suite results contained in `actual_suites` and the directory specified by `root`
/// contain the suites, results, artifacts, and test cases in `expected_suite`.
/// Note that this currently does not support duplicate suite names.
fn assert_suite_results(
    root: &Path,
    actual_suites: &Vec<SuiteResult<'_>>,
    expected_suites: &Vec<ExpectedSuite>,
) {
    assert_eq!(actual_suites.len(), expected_suites.len());
    let mut expected_suites_map = HashMap::new();
    for suite in expected_suites.iter() {
        expected_suites_map.insert(suite.name.clone(), suite);
    }
    assert_eq!(
        actual_suites.len(),
        expected_suites_map.len(),
        "Run contains multiple suites with the same name. \
        This is currently unsupported by assert_suite_results"
    );
    for suite in actual_suites.iter() {
        assert_suite_result(
            root,
            suite,
            expected_suites_map
                .get(&suite.common.deref().name)
                .expect("No matching expected suite"),
        );
    }
}

/// Assert that the suite results contained in `actual_suite` and the directory specified by `root`
/// contain the results, artifacts, and test cases in `expected_suite`.
pub fn assert_suite_result(
    root: &Path,
    actual_suite: &SuiteResult<'_>,
    expected_suite: &ExpectedSuite,
) {
    let context = EntityContext::Suite(expected_suite);
    let &SuiteResult { common, cases, tags } = &actual_suite;
    assert_eq!(common.deref().outcome, expected_suite.outcome, "Outcome for {}", context);
    assert_eq!(common.deref().name, expected_suite.name, "Name for {}", context);
    assert_match_option!(
        expected_suite.duration_milliseconds,
        common.deref().duration_milliseconds,
        format!("Duration for {}", context)
    );
    assert_match_option!(
        expected_suite.start_time,
        common.deref().start_time,
        format!("Start time for {}", context)
    );

    let mut tags: Vec<TestTag> = tags.clone().into_owned();
    tags.sort();

    let mut expected_tags = expected_suite.tags.clone();
    expected_tags.sort();

    assert_eq!(tags, expected_tags);

    assert_artifacts(
        root,
        &common.deref().artifact_dir.root,
        &common.deref().artifact_dir.artifacts,
        &expected_suite.artifacts,
        context,
    );

    assert_eq!(cases.len(), expected_suite.cases.len());
    for case in cases.iter() {
        let expected_case = expected_suite.cases.get(&case.common.deref().name);
        assert!(
            expected_case.is_some(),
            "Found unexpected case {} in {}",
            case.common.deref().name,
            context
        );
        assert_case_result(root, case, expected_case.unwrap(), expected_suite);
    }
}

fn assert_case_result(
    root: &Path,
    actual_case: &TestCaseResult<'_>,
    expected_case: &ExpectedTestCase,
    parent_suite: &ExpectedSuite,
) {
    let context = EntityContext::Case(parent_suite, expected_case);
    assert_eq!(actual_case.common.deref().name, expected_case.name, "Name for {}", context);
    assert_eq!(
        actual_case.common.deref().outcome,
        expected_case.outcome,
        "Outcome for {}",
        context
    );
    assert_match_option!(
        expected_case.duration_milliseconds,
        actual_case.common.deref().duration_milliseconds,
        format!("Duration for {}", context)
    );
    assert_match_option!(
        expected_case.start_time,
        actual_case.common.deref().start_time,
        format!("Start time for {}", context)
    );
    assert_artifacts(
        root,
        &actual_case.common.deref().artifact_dir.root,
        &actual_case.common.deref().artifact_dir.artifacts,
        &expected_case.artifacts,
        context,
    );
}

fn assert_artifacts(
    root: &Path,
    artifact_dir: &Path,
    actual_artifacts: &HashMap<PathBuf, ArtifactMetadata>,
    expected_artifacts: &ArtifactMetadataToAssertionMap,
    entity_context: EntityContext<'_>,
) {
    // TODO(fxbug.dev/100463): add options so that the test author can explicitly declare whether
    // artifacts should be an exact match, should contain (and may contain more) artifacts,
    // or any number of artifacts is accesptable.
    // This skips artifact assertion for the typical case where verifying artifacts isn't
    // necessary and allows the author to avoid listing out every artifact that is generated
    // by the test.
    if expected_artifacts.is_empty() {
        return;
    }

    let actual_artifacts_by_metadata: HashMap<ArtifactMetadata, PathBuf> =
        actual_artifacts.iter().map(|(key, value)| (value.clone(), key.clone())).collect();
    // For now, artifact metadata should be unique for each artifact.
    assert_eq!(
        actual_artifacts_by_metadata.len(),
        actual_artifacts.len(),
        "Artifacts for {} do not have unique metadata. Actual artifacts: {:?}",
        entity_context,
        actual_artifacts
    );

    let expected_metadata: HashSet<_> = expected_artifacts.keys().collect();
    let actual_metadata: HashSet<_> = actual_artifacts_by_metadata.keys().collect();

    assert_eq!(
        expected_metadata, actual_metadata,
        "Artifacts for {} do not have matching metadata.",
        entity_context,
    );

    for (expected_metadata, expected_artifact) in expected_artifacts.iter() {
        let actual_filepath =
            artifact_dir.join(actual_artifacts_by_metadata.get(expected_metadata).unwrap());
        match expected_artifact {
            ExpectedArtifact::File { name, assertion_fn } => {
                assert_file(
                    &root.join(&actual_filepath),
                    name,
                    assertion_fn,
                    ArtifactContext { entity: &entity_context, metadata: expected_metadata },
                );
            }
            ExpectedArtifact::Directory { files, name } => {
                match name {
                    None => (),
                    Some(name) => assert_eq!(
                        name.as_str(),
                        actual_filepath.file_name().unwrap().to_str().unwrap(),
                        "Expected filename {} for artifact matching {:?} but got {}",
                        name,
                        expected_metadata,
                        actual_filepath.file_name().unwrap().to_str().unwrap()
                    ),
                }
                let actual_entries: HashSet<_> = std::fs::read_dir(root.join(&actual_filepath))
                    .expect("Failed to read directory artifact path")
                    .map(|entry| match entry {
                        Ok(dir_entry) if dir_entry.file_type().unwrap().is_file() => {
                            dir_entry.file_name().to_str().unwrap().to_string()
                        }
                        // TODO(fxbugdev/85528) - support directory artifacts with subdirectories
                        Ok(_) => panic!("Directory artifact with subdirectories unsupported"),
                        Err(e) => panic!("Error reading directory artifact: {:?}", e),
                    })
                    .collect();
                let expected_entries: HashSet<_> =
                    files.iter().map(|(name, _)| name.to_string()).collect();
                assert_eq!(
                    actual_entries, expected_entries,
                    "Expected files {:?} in directory artifact, got {:?}",
                    &expected_entries, &actual_entries
                );
                for (name, assertion) in files {
                    assert_file(
                        &root.join(&actual_filepath).join(name),
                        &None,
                        assertion,
                        ArtifactContext { entity: &entity_context, metadata: expected_metadata },
                    );
                }
            }
        }
    }
}

fn assert_file(
    file_path: &Path,
    name: &Option<String>,
    assertion_fn: &Box<dyn Fn(&str)>,
    artifact_context: ArtifactContext<'_, '_>,
) {
    match name {
        None => (),
        Some(name) => assert_eq!(
            name.as_str(),
            file_path.file_name().unwrap().to_str().unwrap(),
            "Got incorrect filename while checking file for artifact {}",
            artifact_context
        ),
    }
    let actual_contents = std::fs::read_to_string(&file_path);
    (assertion_fn)(&actual_contents.unwrap());
}

/// The expected contents of an artifact.
enum ExpectedArtifact {
    /// An artifact contained in a single file, such as stdout.
    File {
        /// If given, the expected name of the file.
        name: Option<String>,
        /// Assertion run against the contents of the file.
        assertion_fn: Box<dyn Fn(&str)>,
    },
    /// An artifact consisting of files in a directory.
    Directory {
        /// List of expected files, as (name, assertion) pairs. The name
        /// is the expected name of the file, and the assertion fn is run
        /// against the contents of the file.
        files: Vec<(String, Box<dyn Fn(&str)>)>,
        /// If given, the expected name of the directory.
        name: Option<String>,
    },
}

/// Contents of an expected directory artifact.
pub struct ExpectedDirectory {
    files: Vec<(String, Box<dyn Fn(&str)>)>,
}

impl ExpectedDirectory {
    /// Create a new empty expected directory.
    pub fn new() -> Self {
        Self { files: vec![] }
    }

    /// Add a file with expected |contents|.
    pub fn with_file(self, name: impl AsRef<str>, contents: impl AsRef<str>) -> Self {
        let owned_expected = contents.as_ref().to_string();
        let owned_name = name.as_ref().to_string();
        self.with_matching_file(name, move |actual| {
            assert_eq!(
                &owned_expected, actual,
                "Mismatch in contents of file {}. Expected: '{}', actual:'{}'",
                owned_name, &owned_expected, actual
            )
        })
    }

    pub fn with_matching_file(
        mut self,
        name: impl AsRef<str>,
        matcher: impl 'static + Fn(&str),
    ) -> Self {
        self.files.push((name.as_ref().to_string(), Box::new(matcher)));
        self
    }
}

/// A version of a test run result that contains all output in memory. This should only be used
/// for making assertions in a test.
pub struct ExpectedTestRun {
    artifacts: ArtifactMetadataToAssertionMap,
    outcome: MaybeUnknown<Outcome>,
    start_time: MatchOption<u64>,
    duration_milliseconds: MatchOption<u64>,
    suites: Vec<ExpectedSuite>,
}

/// A version of a suite run result that contains all output in memory. This should only be used
/// for making assertions in a test.
pub struct ExpectedSuite {
    artifacts: ArtifactMetadataToAssertionMap,
    name: String,
    outcome: MaybeUnknown<Outcome>,
    cases: HashMap<String, ExpectedTestCase>,
    start_time: MatchOption<u64>,
    duration_milliseconds: MatchOption<u64>,
    tags: Vec<TestTag>,
}

/// A version of a test case result that contains all output in memory. This should only be used
/// for making assertions in a test.
pub struct ExpectedTestCase {
    artifacts: ArtifactMetadataToAssertionMap,
    name: String,
    outcome: MaybeUnknown<Outcome>,
    start_time: MatchOption<u64>,
    duration_milliseconds: MatchOption<u64>,
}

macro_rules! common_impl {
    {} => {
        /// Add an artifact matching the exact contents. Artifacts are checked by finding
        /// an entry matching the given metadata, then checking the contents of the corresponding
        /// file. If |name| is provided, the name of the file is verified. Artifacts are keyed by
        /// metadata rather than by name as the names of files are not guaranteed to be stable.
        pub fn with_artifact<S, T, U>(
            self, metadata: U, name: Option<S>, contents: T
        ) -> Self
        where
            S: AsRef<str>,
            T: AsRef<str>,
            U: Into<ArtifactMetadata>
        {
            let owned_expected = contents.as_ref().to_string();
            let metadata = metadata.into();
            let metadata_clone = metadata.clone();
            self.with_matching_artifact(metadata, name, move |actual| {
                assert_eq!(
                    &owned_expected, actual,
                    "Mismatch in artifact with metadata {:?}. Expected: '{}', actual:'{}'",
                    metadata_clone, &owned_expected, actual
                )
            })
        }

        /// Add an artifact matching the exact contents. Artifacts are checked by finding
        /// an entry matching the given metadata, then running |matcher| against the contents of
        /// the file. If |name| is provided, the name of the file is verified. Artifacts are keyed
        /// by metadata rather than by name as the names of files are not guaranteed to be stable.
        pub fn with_matching_artifact<S, F, U>(
            mut self,
            metadata: U,
            name: Option<S>,
            matcher: F,
        ) -> Self
        where
            S: AsRef<str>,
            F: 'static + Fn(&str),
            U: Into<ArtifactMetadata>
        {
            self.artifacts.insert(
                metadata.into(),
                ExpectedArtifact::File {
                    name: name.map(|s| s.as_ref().to_string()),
                    assertion_fn: Box::new(matcher),
                }
            );
            self
        }

        /// Add a directory based artifact containing the entries described in |directory|.
        pub fn with_directory_artifact<S, U>(
            mut self,
            metadata: U,
            name: Option<S>,
            directory: ExpectedDirectory,
        ) -> Self
        where
            S: AsRef<str>,
            U: Into<ArtifactMetadata>
        {
            self.artifacts.insert(
                metadata.into(),
                ExpectedArtifact::Directory {
                    name: name.map(|s| s.as_ref().to_string()),
                    files: directory.files,
                }
            );
            self
        }

        /// Verify an exact start time.
        pub fn with_start_time(mut self, millis: u64) -> Self {
            self.start_time = MatchOption::Specified(millis);
            self
        }

        /// Verify an exact run duration.
        pub fn with_run_duration(mut self, millis: u64) -> Self {
            self.duration_milliseconds = MatchOption::Specified(millis);
            self
        }

        /// Verify that a start time is present.
        pub fn with_any_start_time(mut self) -> Self {
            self.start_time = MatchOption::Any;
            self
        }

        /// Verify that a run duration is present.
        pub fn with_any_run_duration(mut self) -> Self {
            self.duration_milliseconds = MatchOption::Any;
            self
        }

        /// Verify that no start time is present.
        pub fn with_no_start_time(mut self) -> Self {
            self.start_time = MatchOption::None;
            self
        }

        /// Verify that no run duration is present.
        pub fn with_no_run_duration(mut self) -> Self {
            self.duration_milliseconds = MatchOption::None;
            self
        }
    };
}

impl ExpectedTestRun {
    /// Create a new `ExpectedTestRun` with the given `outcome`.
    pub fn new(outcome: Outcome) -> Self {
        Self {
            artifacts: ArtifactMetadataToAssertionMap::new(),
            outcome: outcome.into(),
            start_time: MatchOption::AnyOrNone,
            duration_milliseconds: MatchOption::AnyOrNone,
            suites: vec![],
        }
    }

    pub fn with_suite(mut self, suite: ExpectedSuite) -> Self {
        self.suites.push(suite);
        self
    }

    common_impl! {}
}

impl ExpectedSuite {
    /// Create a new `ExpectedTestRun` with the given `name` and `outcome`.
    pub fn new<S: AsRef<str>>(name: S, outcome: Outcome) -> Self {
        Self {
            artifacts: ArtifactMetadataToAssertionMap::new(),
            name: name.as_ref().to_string(),
            outcome: outcome.into(),
            cases: HashMap::new(),
            start_time: MatchOption::AnyOrNone,
            duration_milliseconds: MatchOption::AnyOrNone,
            tags: vec![],
        }
    }

    /// Add a test case to the suite.
    pub fn with_case(mut self, case: ExpectedTestCase) -> Self {
        self.cases.insert(case.name.clone(), case);
        self
    }

    /// Add a tag to the suite.
    pub fn with_tag(mut self, tag: TestTag) -> Self {
        self.tags.push(tag);
        self
    }

    common_impl! {}
}

impl ExpectedTestCase {
    /// Create a new `ExpectedTestCase` with the given `name` and `outcome`.
    pub fn new<S: AsRef<str>>(name: S, outcome: Outcome) -> Self {
        Self {
            artifacts: ArtifactMetadataToAssertionMap::new(),
            name: name.as_ref().to_string(),
            outcome: outcome.into(),
            start_time: MatchOption::AnyOrNone,
            duration_milliseconds: MatchOption::AnyOrNone,
        }
    }

    common_impl! {}
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::{ArtifactType, CommonResult, OutputDirectoryBuilder, SchemaVersion, RUN_NAME};
    use std::borrow::Cow;
    use std::io::Write;

    fn test_with_directory<F: Fn(OutputDirectoryBuilder)>(_test_name: &str, test_fn: F) {
        for version in SchemaVersion::all_variants() {
            let dir = tempfile::TempDir::new().unwrap();
            let directory_builder =
                OutputDirectoryBuilder::new(dir.path(), version).expect("Create directory builder");
            test_fn(directory_builder);
        }
    }

    #[fixture::fixture(test_with_directory)]
    #[test]
    fn assert_run_result_check_outcome_only(output_dir: OutputDirectoryBuilder) {
        let actual = TestRunResult {
            common: Cow::Owned(CommonResult {
                name: RUN_NAME.to_string(),
                artifact_dir: output_dir.new_artifact_dir().expect("new artifact dir"),
                outcome: Outcome::Passed.into(),
                start_time: Some(64),
                duration_milliseconds: Some(128),
            }),
            suites: vec![],
        };

        output_dir.save_summary(&actual).expect("save summary");
        assert_run_result(
            output_dir.path(),
            &ExpectedTestRun::new(Outcome::Passed).with_any_start_time().with_any_run_duration(),
        );
    }

    #[fixture::fixture(test_with_directory)]
    #[test]
    fn assert_run_result_check_exact_timing(output_dir: OutputDirectoryBuilder) {
        let actual = TestRunResult {
            common: Cow::Owned(CommonResult {
                name: RUN_NAME.to_string(),
                artifact_dir: output_dir.new_artifact_dir().expect("new artifact dir"),
                outcome: Outcome::Passed.into(),
                start_time: Some(64),
                duration_milliseconds: Some(128),
            }),
            suites: vec![],
        };

        output_dir.save_summary(&actual).expect("save summary");
        assert_run_result(
            output_dir.path(),
            &ExpectedTestRun::new(Outcome::Passed).with_start_time(64).with_run_duration(128),
        );
    }

    #[fixture::fixture(test_with_directory)]
    #[test]
    fn assert_run_result_check_timing_unspecified(output_dir: OutputDirectoryBuilder) {
        let actual = TestRunResult {
            common: Cow::Owned(CommonResult {
                name: RUN_NAME.to_string(),
                artifact_dir: output_dir.new_artifact_dir().expect("new artifact dir"),
                outcome: Outcome::Passed.into(),
                start_time: None,
                duration_milliseconds: None,
            }),
            suites: vec![],
        };

        output_dir.save_summary(&actual).expect("save summary");
        assert_run_result(
            output_dir.path(),
            &ExpectedTestRun::new(Outcome::Passed).with_no_start_time().with_no_run_duration(),
        );
    }

    #[fixture::fixture(test_with_directory)]
    #[test]
    fn assert_run_result_single_artifact_unspecified_name(output_dir: OutputDirectoryBuilder) {
        let mut artifact_dir = output_dir.new_artifact_dir().expect("new artifact dir");
        let mut artifact =
            artifact_dir.new_artifact(ArtifactType::Syslog, "b.txt").expect("create artifact");
        write!(artifact, "hello").expect("write to artifact");
        drop(artifact);

        let actual = TestRunResult {
            common: Cow::Owned(CommonResult {
                name: RUN_NAME.to_string(),
                artifact_dir,
                outcome: Outcome::Passed.into(),
                start_time: None,
                duration_milliseconds: None,
            }),
            suites: vec![],
        };

        output_dir.save_summary(&actual).expect("save summary");
        assert_run_result(
            output_dir.path(),
            &ExpectedTestRun::new(Outcome::Passed).with_artifact(
                ArtifactType::Syslog,
                Option::<&str>::None,
                "hello",
            ),
        );
    }

    #[fixture::fixture(test_with_directory)]
    #[test]
    fn assert_run_result_single_artifact_specified_name(output_dir: OutputDirectoryBuilder) {
        let mut artifact_dir = output_dir.new_artifact_dir().expect("new artifact dir");
        let mut artifact =
            artifact_dir.new_artifact(ArtifactType::Syslog, "b.txt").expect("create artifact");
        write!(artifact, "hello").expect("write to artifact");
        drop(artifact);

        let actual = TestRunResult {
            common: Cow::Owned(CommonResult {
                name: RUN_NAME.to_string(),
                artifact_dir,
                outcome: Outcome::Passed.into(),
                start_time: None,
                duration_milliseconds: None,
            }),
            suites: vec![],
        };

        output_dir.save_summary(&actual).expect("save summary");
        assert_run_result(
            output_dir.path(),
            &ExpectedTestRun::new(Outcome::Passed).with_artifact(
                ArtifactType::Syslog,
                "b.txt".into(),
                "hello",
            ),
        );
    }

    #[fixture::fixture(test_with_directory)]
    #[test]
    #[should_panic(expected = "Outcome for TEST RUN")]
    fn assert_run_outcome_mismatch(output_dir: OutputDirectoryBuilder) {
        let actual = TestRunResult {
            common: Cow::Owned(CommonResult {
                name: RUN_NAME.to_string(),
                artifact_dir: output_dir.new_artifact_dir().expect("new artifact dir"),
                outcome: Outcome::Failed.into(),
                start_time: None,
                duration_milliseconds: None,
            }),
            suites: vec![],
        };

        output_dir.save_summary(&actual).expect("save summary");
        assert_run_result(output_dir.path(), &ExpectedTestRun::new(Outcome::Passed));
    }

    #[fixture::fixture(test_with_directory)]
    #[test]
    #[should_panic(expected = "Start time for TEST RUN")]
    fn assert_run_start_time_mismatch(output_dir: OutputDirectoryBuilder) {
        let actual = TestRunResult {
            common: Cow::Owned(CommonResult {
                name: RUN_NAME.to_string(),
                artifact_dir: output_dir.new_artifact_dir().expect("new artifact dir"),
                outcome: Outcome::Failed.into(),
                start_time: Some(64),
                duration_milliseconds: None,
            }),
            suites: vec![],
        };

        output_dir.save_summary(&actual).expect("save summary");
        assert_run_result(
            output_dir.path(),
            &ExpectedTestRun::new(Outcome::Passed).with_start_time(23),
        );
    }

    #[fixture::fixture(test_with_directory)]
    #[test]
    #[should_panic(expected = "Run duration for TEST RUN")]
    fn assert_run_duration_mismatch(output_dir: OutputDirectoryBuilder) {
        let actual = TestRunResult {
            common: Cow::Owned(CommonResult {
                name: RUN_NAME.to_string(),
                artifact_dir: output_dir.new_artifact_dir().expect("new artifact dir"),
                outcome: Outcome::Failed.into(),
                start_time: None,
                duration_milliseconds: None,
            }),
            suites: vec![],
        };

        output_dir.save_summary(&actual).expect("save summary");
        assert_run_result(
            output_dir.path(),
            &ExpectedTestRun::new(Outcome::Passed).with_run_duration(23),
        );
    }

    #[fixture::fixture(test_with_directory)]
    #[test]
    #[should_panic]
    fn assert_run_artifact_mismatch(output_dir: OutputDirectoryBuilder) {
        let mut artifact_dir = output_dir.new_artifact_dir().expect("new artifact dir");
        let mut artifact =
            artifact_dir.new_artifact(ArtifactType::Syslog, "missing").expect("create artifact");
        write!(artifact, "hello").expect("write to artifact");
        drop(artifact);

        let actual = TestRunResult {
            common: Cow::Owned(CommonResult {
                name: RUN_NAME.to_string(),
                artifact_dir,
                outcome: Outcome::Failed.into(),
                start_time: None,
                duration_milliseconds: None,
            }),
            suites: vec![],
        };

        output_dir.save_summary(&actual).expect("save summary");
        assert_run_result(
            output_dir.path(),
            &ExpectedTestRun::new(Outcome::Failed).with_artifact(
                ArtifactType::Stderr,
                "stderr.txt".into(),
                "",
            ),
        );
    }

    fn passing_run_with_single_suite<'a>(
        output_dir: &OutputDirectoryBuilder,
        suite: SuiteResult<'a>,
    ) -> TestRunResult<'a> {
        TestRunResult {
            common: Cow::Owned(CommonResult {
                name: RUN_NAME.to_string(),
                artifact_dir: output_dir.new_artifact_dir().expect("new artifact dir"),
                outcome: Outcome::Passed.into(),
                start_time: Some(64),
                duration_milliseconds: Some(128),
            }),
            suites: vec![suite],
        }
    }

    #[fixture::fixture(test_with_directory)]
    #[test]
    fn assert_run_result_with_suite(output_dir: OutputDirectoryBuilder) {
        let actual = passing_run_with_single_suite(
            &output_dir,
            SuiteResult {
                common: Cow::Owned(CommonResult {
                    name: "suite".to_string(),
                    artifact_dir: output_dir.new_artifact_dir().expect("new artifact dir"),
                    outcome: Outcome::Passed.into(),
                    start_time: Some(64),
                    duration_milliseconds: Some(128),
                }),
                cases: vec![],
                tags: Cow::Owned(vec![]),
            },
        );

        output_dir.save_summary(&actual).expect("save summary");
        assert_run_result(
            output_dir.path(),
            &ExpectedTestRun::new(Outcome::Passed)
                .with_any_start_time()
                .with_any_run_duration()
                .with_suite(
                    ExpectedSuite::new("suite", Outcome::Passed)
                        .with_any_start_time()
                        .with_any_run_duration(),
                ),
        );
    }

    #[fixture::fixture(test_with_directory)]
    #[test]
    fn assert_run_result_with_suite_exact_times(output_dir: OutputDirectoryBuilder) {
        let actual = passing_run_with_single_suite(
            &output_dir,
            SuiteResult {
                common: Cow::Owned(CommonResult {
                    name: "suite".to_string(),
                    artifact_dir: output_dir.new_artifact_dir().expect("new artifact dir"),
                    outcome: Outcome::Passed.into(),
                    start_time: Some(64),
                    duration_milliseconds: Some(128),
                }),
                cases: vec![],
                tags: Cow::Owned(vec![]),
            },
        );

        output_dir.save_summary(&actual).expect("save summary");
        assert_run_result(
            output_dir.path(),
            &ExpectedTestRun::new(Outcome::Passed)
                .with_any_start_time()
                .with_any_run_duration()
                .with_suite(
                    ExpectedSuite::new("suite", Outcome::Passed)
                        .with_start_time(64)
                        .with_run_duration(128),
                ),
        );
    }

    #[fixture::fixture(test_with_directory)]
    #[test]
    fn assert_run_result_with_suite_no_times(output_dir: OutputDirectoryBuilder) {
        let actual = passing_run_with_single_suite(
            &output_dir,
            SuiteResult {
                common: Cow::Owned(CommonResult {
                    name: "suite".to_string(),
                    artifact_dir: output_dir.new_artifact_dir().expect("new artifact dir"),
                    outcome: Outcome::Passed.into(),
                    start_time: None,
                    duration_milliseconds: None,
                }),
                cases: vec![],
                tags: Cow::Owned(vec![]),
            },
        );

        output_dir.save_summary(&actual).expect("save summary");
        assert_run_result(
            output_dir.path(),
            &ExpectedTestRun::new(Outcome::Passed)
                .with_any_start_time()
                .with_any_run_duration()
                .with_suite(
                    ExpectedSuite::new("suite", Outcome::Passed)
                        .with_no_start_time()
                        .with_no_run_duration(),
                ),
        );
    }

    #[fixture::fixture(test_with_directory)]
    #[test]
    fn assert_run_result_suite_with_artifact(output_dir: OutputDirectoryBuilder) {
        let mut artifact_dir = output_dir.new_artifact_dir().expect("new artifact dir");
        let mut artifact =
            artifact_dir.new_artifact(ArtifactType::Syslog, "b.txt").expect("create artifact");
        write!(artifact, "hello").expect("write to artifact");
        drop(artifact);

        let actual = passing_run_with_single_suite(
            &output_dir,
            SuiteResult {
                common: Cow::Owned(CommonResult {
                    name: "suite".to_string(),
                    artifact_dir,
                    outcome: Outcome::Passed.into(),
                    start_time: None,
                    duration_milliseconds: None,
                }),
                cases: vec![],
                tags: Cow::Owned(vec![]),
            },
        );

        output_dir.save_summary(&actual).expect("save summary");
        assert_run_result(
            output_dir.path(),
            &ExpectedTestRun::new(Outcome::Passed)
                .with_any_start_time()
                .with_any_run_duration()
                .with_suite(ExpectedSuite::new("suite", Outcome::Passed).with_artifact(
                    ArtifactType::Syslog,
                    "b.txt".into(),
                    "hello",
                )),
        );
    }

    #[fixture::fixture(test_with_directory)]
    #[test]
    fn assert_run_result_suite_with_case(output_dir: OutputDirectoryBuilder) {
        let actual = passing_run_with_single_suite(
            &output_dir,
            SuiteResult {
                common: Cow::Owned(CommonResult {
                    name: "suite".to_string(),
                    artifact_dir: output_dir.new_artifact_dir().expect("new artifact dir"),
                    outcome: Outcome::Passed.into(),
                    start_time: None,
                    duration_milliseconds: None,
                }),
                cases: vec![TestCaseResult {
                    common: Cow::Owned(CommonResult {
                        name: "case".to_string(),
                        artifact_dir: output_dir.new_artifact_dir().expect("new artifact dir"),
                        outcome: Outcome::Passed.into(),
                        start_time: None,
                        duration_milliseconds: None,
                    }),
                }],
                tags: Cow::Owned(vec![]),
            },
        );

        output_dir.save_summary(&actual).expect("save summary");
        assert_run_result(
            output_dir.path(),
            &ExpectedTestRun::new(Outcome::Passed)
                .with_any_start_time()
                .with_any_run_duration()
                .with_suite(
                    ExpectedSuite::new("suite", Outcome::Passed).with_case(
                        ExpectedTestCase::new("case", Outcome::Passed)
                            .with_no_run_duration()
                            .with_no_start_time(),
                    ),
                ),
        );
    }

    #[fixture::fixture(test_with_directory)]
    #[test]
    fn assert_run_result_suite_with_tags(output_dir: OutputDirectoryBuilder) {
        let actual = passing_run_with_single_suite(
            &output_dir,
            SuiteResult {
                common: Cow::Owned(CommonResult {
                    name: "suite".to_string(),
                    artifact_dir: output_dir.new_artifact_dir().expect("new artifact dir"),
                    outcome: Outcome::Passed.into(),
                    start_time: None,
                    duration_milliseconds: None,
                }),
                cases: vec![],
                tags: Cow::Owned(vec![
                    TestTag { key: "os".to_string(), value: "fuchsia".to_string() },
                    TestTag { key: "cpu".to_string(), value: "arm64".to_string() },
                ]),
            },
        );

        output_dir.save_summary(&actual).expect("save summary");
        assert_run_result(
            output_dir.path(),
            &ExpectedTestRun::new(Outcome::Passed)
                .with_any_start_time()
                .with_any_run_duration()
                .with_suite(
                    ExpectedSuite::new("suite", Outcome::Passed)
                        .with_tag(TestTag { key: "cpu".to_string(), value: "arm64".to_string() })
                        .with_tag(TestTag { key: "os".to_string(), value: "fuchsia".to_string() }),
                ),
        );
    }

    #[fixture::fixture(test_with_directory)]
    #[test]
    #[should_panic(expected = "Outcome for SUITE suite")]
    fn assert_suite_outcome_mismatch(output_dir: OutputDirectoryBuilder) {
        let actual = passing_run_with_single_suite(
            &output_dir,
            SuiteResult {
                common: Cow::Owned(CommonResult {
                    name: "suite".to_string(),
                    artifact_dir: output_dir.new_artifact_dir().expect("new artifact dir"),
                    outcome: Outcome::Failed.into(),
                    start_time: None,
                    duration_milliseconds: None,
                }),
                cases: vec![],
                tags: Cow::Owned(vec![]),
            },
        );

        output_dir.save_summary(&actual).expect("save summary");
        assert_run_result(
            output_dir.path(),
            &ExpectedTestRun::new(Outcome::Passed)
                .with_any_start_time()
                .with_any_run_duration()
                .with_suite(ExpectedSuite::new("suite", Outcome::Passed)),
        );
    }

    #[fixture::fixture(test_with_directory)]
    #[test]
    #[should_panic(expected = "Start time for SUITE suite")]
    fn assert_suite_start_time_mismatch(output_dir: OutputDirectoryBuilder) {
        let actual = passing_run_with_single_suite(
            &output_dir,
            SuiteResult {
                common: Cow::Owned(CommonResult {
                    name: "suite".to_string(),
                    artifact_dir: output_dir.new_artifact_dir().expect("new artifact dir"),
                    outcome: Outcome::Passed.into(),
                    start_time: None,
                    duration_milliseconds: Some(128),
                }),
                cases: vec![],
                tags: Cow::Owned(vec![]),
            },
        );

        output_dir.save_summary(&actual).expect("save summary");
        assert_run_result(
            output_dir.path(),
            &ExpectedTestRun::new(Outcome::Passed)
                .with_any_start_time()
                .with_any_run_duration()
                .with_suite(ExpectedSuite::new("suite", Outcome::Passed).with_any_start_time()),
        );
    }

    #[fixture::fixture(test_with_directory)]
    #[test]
    #[should_panic(expected = "Duration for SUITE suite")]
    fn assert_suite_duration_mismatch(output_dir: OutputDirectoryBuilder) {
        let actual = passing_run_with_single_suite(
            &output_dir,
            SuiteResult {
                common: Cow::Owned(CommonResult {
                    name: "suite".to_string(),
                    artifact_dir: output_dir.new_artifact_dir().expect("new artifact dir"),
                    outcome: Outcome::Passed.into(),
                    start_time: None,
                    duration_milliseconds: Some(128),
                }),
                cases: vec![],
                tags: Cow::Owned(vec![]),
            },
        );

        output_dir.save_summary(&actual).expect("save summary");
        assert_run_result(
            output_dir.path(),
            &ExpectedTestRun::new(Outcome::Passed)
                .with_any_start_time()
                .with_any_run_duration()
                .with_suite(ExpectedSuite::new("suite", Outcome::Passed).with_run_duration(32)),
        );
    }

    #[fixture::fixture(test_with_directory)]
    #[test]
    #[should_panic]
    fn assert_suite_artifact_mismatch(output_dir: OutputDirectoryBuilder) {
        let actual = passing_run_with_single_suite(
            &output_dir,
            SuiteResult {
                common: Cow::Owned(CommonResult {
                    name: "suite".to_string(),
                    artifact_dir: output_dir.new_artifact_dir().expect("new artifact dir"),
                    outcome: Outcome::Passed.into(),
                    start_time: None,
                    duration_milliseconds: Some(128),
                }),
                cases: vec![],
                tags: Cow::Owned(vec![]),
            },
        );

        output_dir.save_summary(&actual).expect("save summary");
        assert_run_result(
            output_dir.path(),
            &ExpectedTestRun::new(Outcome::Passed)
                .with_any_start_time()
                .with_any_run_duration()
                .with_suite(ExpectedSuite::new("suite", Outcome::Passed).with_artifact(
                    ArtifactType::Stderr,
                    Option::<&str>::None,
                    "missing contents",
                )),
        );
    }

    #[fixture::fixture(test_with_directory)]
    #[test]
    #[should_panic(expected = "Found unexpected case")]
    fn assert_suite_case_mismatch(output_dir: OutputDirectoryBuilder) {
        let actual = passing_run_with_single_suite(
            &output_dir,
            SuiteResult {
                common: Cow::Owned(CommonResult {
                    name: "suite".to_string(),
                    artifact_dir: output_dir.new_artifact_dir().expect("new artifact dir"),
                    outcome: Outcome::Failed.into(),
                    start_time: None,
                    duration_milliseconds: None,
                }),
                cases: vec![TestCaseResult {
                    common: Cow::Owned(CommonResult {
                        name: "case".to_string(),
                        artifact_dir: output_dir.new_artifact_dir().expect("new artifact dir"),
                        outcome: Outcome::Passed.into(),
                        start_time: None,
                        duration_milliseconds: None,
                    }),
                }],
                tags: Cow::Owned(vec![]),
            },
        );

        output_dir.save_summary(&actual).expect("save summary");
        assert_run_result(
            output_dir.path(),
            &ExpectedTestRun::new(Outcome::Passed).with_any_start_time().with_suite(
                ExpectedSuite::new("suite", Outcome::Failed)
                    .with_case(ExpectedTestCase::new("wrong name", Outcome::Passed)),
            ),
        );
    }

    #[fixture::fixture(test_with_directory)]
    #[test]
    fn assert_artifacts_empty(output_dir: OutputDirectoryBuilder) {
        let actual = TestRunResult {
            common: Cow::Owned(CommonResult {
                name: RUN_NAME.to_string(),
                artifact_dir: output_dir.new_artifact_dir().expect("new artifact dir"),
                outcome: Outcome::Passed.into(),
                start_time: None,
                duration_milliseconds: None,
            }),
            suites: vec![],
        };

        output_dir.save_summary(&actual).expect("save summary");
        assert_run_result(output_dir.path(), &ExpectedTestRun::new(Outcome::Passed));
    }

    #[fixture::fixture(test_with_directory)]
    #[test]
    fn assert_artifacts_exact_content(output_dir: OutputDirectoryBuilder) {
        let mut artifact_dir = output_dir.new_artifact_dir().expect("new artifact dir");
        let mut artifact =
            artifact_dir.new_artifact(ArtifactType::Stderr, "b.txt").expect("new artifact");
        write!(artifact, "hello").expect("write to artifact");
        let actual = TestRunResult {
            common: Cow::Owned(CommonResult {
                name: RUN_NAME.to_string(),
                artifact_dir,
                outcome: Outcome::Passed.into(),
                start_time: None,
                duration_milliseconds: None,
            }),
            suites: vec![],
        };

        output_dir.save_summary(&actual).expect("save summary");
        assert_run_result(
            output_dir.path(),
            &ExpectedTestRun::new(Outcome::Passed).with_artifact(
                ArtifactType::Stderr,
                Option::<&str>::None,
                "hello",
            ),
        );
    }

    #[fixture::fixture(test_with_directory)]
    #[test]
    fn assert_artifacts_exact_content_exact_name(output_dir: OutputDirectoryBuilder) {
        let mut artifact_dir = output_dir.new_artifact_dir().expect("new artifact dir");
        let mut artifact =
            artifact_dir.new_artifact(ArtifactType::Stderr, "b.txt").expect("new artifact");
        write!(artifact, "hello").expect("write to artifact");
        let actual = TestRunResult {
            common: Cow::Owned(CommonResult {
                name: RUN_NAME.to_string(),
                artifact_dir,
                outcome: Outcome::Passed.into(),
                start_time: None,
                duration_milliseconds: None,
            }),
            suites: vec![],
        };

        output_dir.save_summary(&actual).expect("save summary");
        assert_run_result(
            output_dir.path(),
            &ExpectedTestRun::new(Outcome::Passed).with_artifact(
                ArtifactType::Stderr,
                Some("b.txt"),
                "hello",
            ),
        );
    }

    #[fixture::fixture(test_with_directory)]
    #[test]
    fn assert_artifacts_matching_content(output_dir: OutputDirectoryBuilder) {
        let mut artifact_dir = output_dir.new_artifact_dir().expect("new artifact dir");
        let mut artifact =
            artifact_dir.new_artifact(ArtifactType::Stderr, "b.txt").expect("new artifact");
        write!(artifact, "hello").expect("write to artifact");
        let actual = TestRunResult {
            common: Cow::Owned(CommonResult {
                name: RUN_NAME.to_string(),
                artifact_dir,
                outcome: Outcome::Passed.into(),
                start_time: None,
                duration_milliseconds: None,
            }),
            suites: vec![],
        };

        output_dir.save_summary(&actual).expect("save summary");
        assert_run_result(
            output_dir.path(),
            &ExpectedTestRun::new(Outcome::Passed).with_matching_artifact(
                ArtifactType::Stderr,
                Some("b.txt"),
                |content| assert_eq!(content, "hello"),
            ),
        );
    }

    #[fixture::fixture(test_with_directory)]
    #[test]
    fn assert_artifacts_moniker_specified(output_dir: OutputDirectoryBuilder) {
        let mut artifact_dir = output_dir.new_artifact_dir().expect("new artifact dir");
        let mut artifact = artifact_dir
            .new_artifact(
                ArtifactMetadata {
                    artifact_type: ArtifactType::Syslog.into(),
                    component_moniker: Some("moniker".into()),
                },
                "b.txt",
            )
            .expect("new artifact");
        write!(artifact, "hello").expect("write to artifact");
        let actual = TestRunResult {
            common: Cow::Owned(CommonResult {
                name: RUN_NAME.to_string(),
                artifact_dir,
                outcome: Outcome::Passed.into(),
                start_time: None,
                duration_milliseconds: None,
            }),
            suites: vec![],
        };

        output_dir.save_summary(&actual).expect("save summary");
        assert_run_result(
            output_dir.path(),
            &ExpectedTestRun::new(Outcome::Passed).with_artifact(
                ArtifactMetadata {
                    artifact_type: ArtifactType::Syslog.into(),
                    component_moniker: Some("moniker".into()),
                },
                Some("b.txt"),
                "hello",
            ),
        );
    }

    #[fixture::fixture(test_with_directory)]
    #[test]
    fn assert_artifacts_directory_artifact(output_dir: OutputDirectoryBuilder) {
        let mut artifact_dir = output_dir.new_artifact_dir().expect("new artifact dir");
        let dir_artifact =
            artifact_dir.new_directory_artifact(ArtifactType::Custom, "b").expect("new artifact");
        std::fs::write(dir_artifact.join("c.txt"), "hello c").unwrap();
        std::fs::write(dir_artifact.join("d.txt"), "hello d").unwrap();
        let actual = TestRunResult {
            common: Cow::Owned(CommonResult {
                name: RUN_NAME.to_string(),
                artifact_dir,
                outcome: Outcome::Passed.into(),
                start_time: None,
                duration_milliseconds: None,
            }),
            suites: vec![],
        };

        output_dir.save_summary(&actual).expect("save summary");
        assert_run_result(
            output_dir.path(),
            &ExpectedTestRun::new(Outcome::Passed).with_directory_artifact(
                ArtifactType::Custom,
                Some("b"),
                ExpectedDirectory::new()
                    .with_file("c.txt", "hello c")
                    .with_matching_file("d.txt", |contents| assert_eq!(contents, "hello d")),
            ),
        );
    }

    #[fixture::fixture(test_with_directory)]
    #[test]
    #[should_panic(expected = "Artifacts for TEST RUN")]
    fn assert_artifacts_missing(output_dir: OutputDirectoryBuilder) {
        let artifact_dir = output_dir.new_artifact_dir().expect("new artifact dir");
        let actual = TestRunResult {
            common: Cow::Owned(CommonResult {
                name: RUN_NAME.to_string(),
                artifact_dir,
                outcome: Outcome::Passed.into(),
                start_time: None,
                duration_milliseconds: None,
            }),
            suites: vec![],
        };

        output_dir.save_summary(&actual).expect("save summary");
        assert_run_result(
            output_dir.path(),
            &ExpectedTestRun::new(Outcome::Passed).with_artifact(
                ArtifactType::Syslog,
                Some("missing"),
                "missing contents",
            ),
        );
    }

    #[fixture::fixture(test_with_directory)]
    #[test]
    #[should_panic(expected = "Artifacts for TEST RUN")]
    fn assert_artifacts_extra_artifact(output_dir: OutputDirectoryBuilder) {
        let mut artifact_dir = output_dir.new_artifact_dir().expect("new artifact dir");
        let mut file_b =
            artifact_dir.new_artifact(ArtifactType::Stderr, "b.txt").expect("create artifact");
        write!(file_b, "hello").unwrap();
        let mut file_c =
            artifact_dir.new_artifact(ArtifactType::Stdout, "c.txt").expect("create artifact");
        write!(file_c, "hello").unwrap();
        drop(file_b);
        drop(file_c);
        let actual = TestRunResult {
            common: Cow::Owned(CommonResult {
                name: RUN_NAME.to_string(),
                artifact_dir,
                outcome: Outcome::Passed.into(),
                start_time: None,
                duration_milliseconds: None,
            }),
            suites: vec![],
        };

        output_dir.save_summary(&actual).expect("save summary");
        assert_run_result(
            output_dir.path(),
            &ExpectedTestRun::new(Outcome::Passed).with_artifact(
                ArtifactType::Stderr,
                "c.txt".into(),
                "hello",
            ),
        );
    }

    #[fixture::fixture(test_with_directory)]
    #[test]
    #[should_panic]
    fn assert_artifacts_content_not_equal(output_dir: OutputDirectoryBuilder) {
        let mut artifact_dir = output_dir.new_artifact_dir().expect("new artifact dir");
        let mut file_b =
            artifact_dir.new_artifact(ArtifactType::Stderr, "b.txt").expect("create artifact");
        write!(file_b, "wrong content").unwrap();
        drop(file_b);
        let actual = TestRunResult {
            common: Cow::Owned(CommonResult {
                name: RUN_NAME.to_string(),
                artifact_dir,
                outcome: Outcome::Passed.into(),
                start_time: None,
                duration_milliseconds: None,
            }),
            suites: vec![],
        };

        output_dir.save_summary(&actual).expect("save summary");
        assert_run_result(
            output_dir.path(),
            &ExpectedTestRun::new(Outcome::Passed).with_artifact(
                ArtifactType::Syslog,
                Option::<&str>::None,
                "expected content",
            ),
        );
    }

    #[fixture::fixture(test_with_directory)]
    #[test]
    #[should_panic]
    fn assert_artifacts_content_does_not_match(output_dir: OutputDirectoryBuilder) {
        let mut artifact_dir = output_dir.new_artifact_dir().expect("new artifact dir");
        let mut file_b =
            artifact_dir.new_artifact(ArtifactType::Stderr, "b.txt").expect("create artifact");
        write!(file_b, "wrong content").unwrap();
        drop(file_b);
        let actual = TestRunResult {
            common: Cow::Owned(CommonResult {
                name: RUN_NAME.to_string(),
                artifact_dir,
                outcome: Outcome::Passed.into(),
                start_time: None,
                duration_milliseconds: None,
            }),
            suites: vec![],
        };

        output_dir.save_summary(&actual).expect("save summary");
        assert_run_result(
            output_dir.path(),
            &ExpectedTestRun::new(Outcome::Passed).with_matching_artifact(
                ArtifactType::Syslog,
                Option::<&str>::None,
                |content| assert_eq!(content, "expected content"),
            ),
        );
    }

    #[fixture::fixture(test_with_directory)]
    #[test]
    #[should_panic]
    fn assert_artifacts_directory_mismatch(output_dir: OutputDirectoryBuilder) {
        let mut artifact_dir = output_dir.new_artifact_dir().expect("new artifact dir");
        let dir_artifact =
            artifact_dir.new_directory_artifact(ArtifactType::Custom, "b").expect("new artifact");
        std::fs::write(dir_artifact.join("c.txt"), "unexpected file").unwrap();
        let actual = TestRunResult {
            common: Cow::Owned(CommonResult {
                name: RUN_NAME.to_string(),
                artifact_dir,
                outcome: Outcome::Passed.into(),
                start_time: None,
                duration_milliseconds: None,
            }),
            suites: vec![],
        };

        output_dir.save_summary(&actual).expect("save summary");
        assert_run_result(
            output_dir.path(),
            &ExpectedTestRun::new(Outcome::Passed).with_directory_artifact(
                ArtifactType::Custom,
                Option::<&str>::None,
                ExpectedDirectory::new(),
            ),
        );
    }

    #[fixture::fixture(test_with_directory)]
    #[test]
    fn assert_artifacts_not_checked_if_unspecified(output_dir: OutputDirectoryBuilder) {
        let mut artifact_dir = output_dir.new_artifact_dir().expect("new artifact dir");
        let mut file_c =
            artifact_dir.new_artifact(ArtifactType::Stderr, "c.txt").expect("create artifact");
        write!(file_c, "unexpected file").unwrap();
        drop(file_c);
        let actual = TestRunResult {
            common: Cow::Owned(CommonResult {
                name: RUN_NAME.to_string(),
                artifact_dir,
                outcome: Outcome::Passed.into(),
                start_time: None,
                duration_milliseconds: None,
            }),
            suites: vec![],
        };

        output_dir.save_summary(&actual).expect("save summary");
        assert_run_result(output_dir.path(), &ExpectedTestRun::new(Outcome::Passed));
    }
}
