// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use lazy_static::lazy_static;
use regex::Regex;

lazy_static! {
    pub static ref NAME_RE: Regex = Regex::new(r"^[0-9a-z\-\._]{1,100}$").unwrap();
    pub static ref HASH_RE: Regex = Regex::new(r"^[0-9a-z]{64}$").unwrap();
}

pub fn check_resource(input: &str) -> bool {
    for segment in input.split('/') {
        if segment.is_empty() || segment == "." || segment == ".." {
            return false;
        }

        if segment.bytes().find(|c| *c == b'\x00').is_some() {
            return false;
        }
    }

    true
}
