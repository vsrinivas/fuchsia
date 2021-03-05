// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use home::home_dir;
use std::path::PathBuf;

pub fn os_and_release_desc() -> String {
    let os = std::env::consts::OS;
    format!("{} {}", &capitalize_first_letter(os), std::env::consts::ARCH)
}

fn capitalize_first_letter(s: &str) -> String {
    let mut chars = s.chars();
    match chars.next() {
        None => String::new(),
        Some(c) => c.to_uppercase().chain(chars).collect(),
    }
}

pub fn analytics_folder() -> String {
    let mut metrics_path = get_home_dir();
    metrics_path.push(".fuchsia/metrics/");
    let path_str = metrics_path.to_str();
    match path_str {
        Some(v) => String::from(v),
        None => String::from("/tmp/.fuchsia/metrics/"),
    }
}

fn get_home_dir() -> PathBuf {
    match home_dir() {
        Some(dir) => dir,
        None => PathBuf::from("/tmp"),
    }
}

const TEST_ENV_VAR: &'static str = "FUCHSIA_TEST_OUTDIR";
const ANALYTICS_DISABLED_ENV_VAR: &'static str = "FUCHSIA_ANALYTICS_DISABLED";

//const BOT_ENV_VARS: Vec<&str>  = vec!
const BOT_ENV_VARS: &'static [&'static str] = &[
    "TF_BUILD",           // Azure
    "bamboo.buildKey",    // Bamboo
    "BUILDKITE",          // BUILDKITE
    "CIRCLECI",           // Circle
    "CIRRUS_CI",          // Cirrus
    "CODEBUILD_BUILD_ID", // Codebuild
    "UNITTEST_ON_FORGE",  // Forge
    "SWARMING_BOT_ID",    // Fuchsia
    "GITHUB_ACTIONS",     // GitHub Actions
    "GITLAB_CI",          // GitLab
    "HEROKU_TEST_RUN_ID", // Heroku
    "BUILD_ID",           // Hudson & Jenkins
    "TEAMCITY_VERSION",   //Teamcity
    "TRAVIS",             // Travis
];

pub fn is_test_env() -> bool {
    std::env::var(TEST_ENV_VAR).is_ok()
}

pub fn is_fuchsia_analytics_disabled_set() -> bool {
    std::env::var(ANALYTICS_DISABLED_ENV_VAR).is_ok()
}

pub fn is_running_in_ci_bot_env() -> bool {
    BOT_ENV_VARS.iter().any(|env_var| std::env::var(env_var).is_ok())
}

pub fn is_analytics_disabled_by_env() -> bool {
    is_test_env() || is_fuchsia_analytics_disabled_set() || is_running_in_ci_bot_env()
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    // TODO(fxb/69321): isolate the env test from CI env
    #[ignore]
    // Rust tests are run in parallel in threads, which means that this test is
    // disruptive to other tests. There's little ROI to doing some kind of fork
    // dance here, so the test is included, but not run by default.
    pub fn test_is_test_env() {
        std::env::set_var(TEST_ENV_VAR, "somepath");
        assert_eq!(true, is_test_env());
        std::env::remove_var(TEST_ENV_VAR);
        assert_eq!(false, is_test_env());
    }

    #[test]
    #[ignore]
    // Rust tests are run in parallel in threads, which means that this test is
    // disruptive to other tests. There's little ROI to doing some kind of fork
    // dance here, so the test is included, but not run by default.
    pub fn test_is_analytics_disabled_env() {
        std::env::set_var(ANALYTICS_DISABLED_ENV_VAR, "1");
        assert_eq!(true, is_fuchsia_analytics_disabled_set());
        std::env::remove_var(ANALYTICS_DISABLED_ENV_VAR);
        assert_eq!(false, is_fuchsia_analytics_disabled_set());
    }

    #[test]
    // TODO(fxb/69321): isolate the env test from CI env
    #[ignore]
    pub fn test_is_bot_env() {
        std::env::set_var(&"BUILD_ID", "1");
        assert_eq!(true, is_running_in_ci_bot_env());
        std::env::remove_var(&"BUILD_ID");
        assert_eq!(false, is_fuchsia_analytics_disabled_set());
    }

    #[test]
    pub fn test_capitalization() {
        assert_eq!("Hello", capitalize_first_letter("hello"));
    }
}
