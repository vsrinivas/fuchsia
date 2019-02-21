// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use zerocopy::ByteSlice;

pub fn skip<B: ByteSlice>(bytes: B, skip: usize) -> Option<B> {
    if bytes.len() < skip {
        None
    } else {
        Some(bytes.split_at(skip).1)
    }
}
