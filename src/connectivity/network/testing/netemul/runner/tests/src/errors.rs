// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use test_case::test_case;

// TODO(https://fxbug.dev/94623): use a package-local (relative) URL and resolve
// it to an absolute URL via a service provided by the package resolver, to
// avoid having to hardcode the package name here.
fn test_url(component: &str) -> String {
    format!("fuchsia-pkg://fuchsia.com/netemul-runner-errors-tests#meta/{component}.cm")
}

#[test_case(
    "invalid-config",
    // [`test_manager_test_lib::SuiteLaunchError::CaseEnumeration`]
    "Cannot enumerate tests";
    "configuration is provided but is invalid"
)]
#[test_case(
    "missing-config",
    // [`test_manager_test_lib::SuiteLaunchError::CaseEnumeration`]
    "Cannot enumerate tests";
    "configuration is not included in `program` section of manifest"
)]
#[test_case(
    "test-suite-not-used",
    // [`test_manager_test_lib::SuiteLaunchError::CaseEnumeration`]
    "Cannot enumerate tests";
    "the `fuchsia.test/Suite` protocol is not available in the test root component's namespace"
)]
#[fuchsia_async::run_singlethreaded(test)]
async fn invalid_test_configuration(test_component: &str, expected_error: &str) {
    let test_url = test_url(test_component);
    let err = netemul_runner_tests::run_test(&test_url)
        .await
        .expect_err("test execution should fail on invalid config");
    // When a test is improperly configured and fails to run, this manifests as
    // an internal error from the test manager.
    let msg = format!("{:?}", err);
    assert!(
        msg.contains(expected_error),
        "unexpected error: expected '{}', got '{}'",
        expected_error,
        msg,
    );
}
