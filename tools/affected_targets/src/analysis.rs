// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        files::file_types_are_supported,
        gn::{Gn, GnAnalyzeInput, GnAnalyzeOutput, GnAnalyzeStatus},
    },
    serde::Serialize,
};

/// `AnalysisResult` signals whether or not a build is required.
///
/// This is different from `GnAnalyzeOutput`, in that an `AnalysisResult`
/// takes into account, for example, if all the file types are of a type
/// where the tool thinks that `gn analyze` will return an accurate result.
#[derive(Debug, Serialize, PartialEq, Clone)]
pub enum AnalysisResult {
    /// Signals that the analysis is confident that a build is required.
    Build,

    /// Signals that the analysis is confident that a build is not required.
    NoBuild,

    /// Signals that the analysis can not determine whether or not a build is
    /// required.
    Unknown(String),
}

/// Checks whether or not a build is required, given a list of `changed_files`.
///
/// This function will return `AnalysisResult::Unknown` if any file types are
/// considered unsupported.
///
/// # Parameters
/// - `changed_files`: The files that have changed.
/// - `gn`: The `Gn` tool to use for the analysis.
/// - `disabled_file_types`: Any file types that have been explicitly disabled by the user.
pub fn is_build_required(changed_files: Vec<String>, gn: impl Gn) -> AnalysisResult {
    if file_types_are_supported(&changed_files) {
        analyze_files(changed_files, gn)
    } else {
        AnalysisResult::Unknown("unsupported file types".to_string())
    }
}

/// Checks whether or not a build is required, given a list of `changed_files`.
///
/// This function expects all `changed_files` to be of a supported type, and will
/// not check them. The `Gn` tool's result will be treated as accurate.
///
/// # Parameters
/// - `changed_files`: The files that have changed.
/// - `gn`: The `Gn` tool to use for the analysis.
fn analyze_files(changed_files: Vec<String>, gn: impl Gn) -> AnalysisResult {
    let analysis_input = GnAnalyzeInput::all_targets(changed_files);
    match gn.analyze(analysis_input) {
        Ok(GnAnalyzeOutput { status, error, .. }) => match error {
            Some(error) => return AnalysisResult::Unknown(format!("gn error: {:?}", error)),
            _ => match status {
                Some(GnAnalyzeStatus::FoundDependency) => AnalysisResult::Build,
                Some(GnAnalyzeStatus::NoDependency) => AnalysisResult::NoBuild,
                _ => AnalysisResult::Unknown("gn analyze inconclusive".to_string()),
            },
        },
        Err(error) => AnalysisResult::Unknown(format!("gn error: {:?}", error)),
    }
}

#[cfg(test)]
mod tests {
    use {super::*, crate::gn, anyhow::Error, matches::assert_matches};

    struct MockGn {
        /// A function that returns the analysis result, allowing clients to
        /// determine what the mock should return.
        analysis_fn: fn(input: GnAnalyzeInput) -> Result<GnAnalyzeOutput, Error>,
    }

    impl MockGn {
        pub fn new(
            analysis_fn: fn(input: GnAnalyzeInput) -> Result<GnAnalyzeOutput, Error>,
        ) -> MockGn {
            MockGn { analysis_fn }
        }
    }

    impl Gn for MockGn {
        fn analyze(&self, input: GnAnalyzeInput) -> Result<GnAnalyzeOutput, Error> {
            (self.analysis_fn)(input)
        }
    }

    /// Invalid file types should never reach the gn analysis stage.
    #[test]
    fn test_invalid_files() {
        let gn = MockGn::new(|_analysis_input| {
            panic!();
        });

        assert_matches!(
            is_build_required(vec!["something.cc".to_string(), "nothing.foo".to_string()], gn,),
            AnalysisResult::Unknown(..)
        );
    }

    /// A gn error results in an analysis unknown.
    #[test]
    fn test_gn_error() {
        let gn = MockGn::new(|_analysis_input| {
            Ok(gn::GnAnalyzeOutput {
                compile_targets: None,
                test_targets: None,
                invalid_targets: None,
                status: None,
                error: Some("gn execution failed".to_string()),
            })
        });

        assert_matches!(
            is_build_required(vec!["something.cc".to_string()], gn),
            AnalysisResult::Unknown(..)
        );
    }

    /// A gn error results in an analysis unknown, even when a status is set.
    #[test]
    fn test_gn_error_and_status() {
        let gn = MockGn::new(|_analysis_input| {
            Ok(gn::GnAnalyzeOutput {
                compile_targets: None,
                test_targets: None,
                invalid_targets: None,
                status: Some(GnAnalyzeStatus::FoundDependency),
                error: Some("gn execution failed".to_string()),
            })
        });

        assert_matches!(
            is_build_required(vec!["something.cc".to_string()], gn),
            AnalysisResult::Unknown(..)
        );
    }

    /// When gn finds a dependency, a Build result is returned.
    #[test]
    fn test_gn_build() {
        let gn = MockGn::new(|_analysis_input| {
            Ok(gn::GnAnalyzeOutput {
                compile_targets: None,
                test_targets: None,
                invalid_targets: None,
                status: Some(GnAnalyzeStatus::FoundDependency),
                error: None,
            })
        });

        assert_eq!(is_build_required(vec!["something.cc".to_string()], gn), AnalysisResult::Build);
    }

    /// When gn does not find a dependency, a NoBuild result is returned.
    #[test]
    fn test_gn_no_build() {
        let gn = MockGn::new(|_analysis_input| {
            Ok(gn::GnAnalyzeOutput {
                compile_targets: None,
                test_targets: None,
                invalid_targets: None,
                status: Some(GnAnalyzeStatus::NoDependency),
                error: None,
            })
        });

        assert_eq!(
            is_build_required(vec!["something.cc".to_string()], gn),
            AnalysisResult::NoBuild
        );
    }

    /// When gn is unsure, a Unknow result is returned.
    #[test]
    fn test_gn_unknown_build() {
        let gn = MockGn::new(|_analysis_input| {
            Ok(gn::GnAnalyzeOutput {
                compile_targets: None,
                test_targets: None,
                invalid_targets: None,
                status: Some(GnAnalyzeStatus::UnknownDependency),
                error: None,
            })
        });

        assert_matches!(
            is_build_required(vec!["something.cc".to_string()], gn),
            AnalysisResult::Unknown(..)
        );
    }
}
