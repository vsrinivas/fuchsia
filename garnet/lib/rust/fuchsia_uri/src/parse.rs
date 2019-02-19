// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
