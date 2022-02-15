// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use assembly_structured_config::{validate_component, ValidationError};
use fuchsia_archive::Reader;
use std::io::Cursor;

fn test_meta_far() -> Reader<Cursor<Vec<u8>>> {
    Reader::new(Cursor::new(include_bytes!(env!("TEST_META_FAR")).to_vec())).unwrap()
}

#[test]
fn config_resolves() {
    validate_component("meta/pass_with_config.cm", &mut test_meta_far()).unwrap();
}

#[test]
fn no_config_passes() {
    validate_component("meta/pass_without_config.cm", &mut test_meta_far()).unwrap();
}

#[test]
fn config_requires_values() {
    match validate_component("meta/fail_missing_config.cm", &mut test_meta_far()).unwrap_err() {
        ValidationError::ConfigValuesMissing { .. } => (),
        other => panic!("expected missing values, got {}", other),
    }
}
