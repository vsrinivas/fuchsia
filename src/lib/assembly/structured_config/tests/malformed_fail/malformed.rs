// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use assembly_structured_config::{validate_component, ValidationError};
use fuchsia_archive::Utf8Reader;
use std::io::Cursor;

const FAIL_MISSING_PROGRAM: &str = "meta/fail_missing_program.cm";
const FAIL_BAD_RUNNER: &str = "meta/fail_bad_runner.cm";

fn malformed_test_meta_far() -> Utf8Reader<Cursor<Vec<u8>>> {
    Utf8Reader::new(Cursor::new(std::fs::read(env!("TEST_META_FAR")).unwrap())).unwrap()
}

#[test]
fn config_requires_program() {
    match validate_component(FAIL_MISSING_PROGRAM, &mut malformed_test_meta_far()).unwrap_err() {
        ValidationError::ProgramMissing => (),
        other => panic!("expected missing program, got {}", other),
    }
}

#[test]
fn config_requires_known_good_runner() {
    match validate_component(FAIL_BAD_RUNNER, &mut malformed_test_meta_far()).unwrap_err() {
        ValidationError::UnsupportedRunner(runner) => assert_eq!(runner, "fake_runner"),
        other => panic!("expected unsupported runner, got {}", other),
    }
}
