// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod hmac_sha1;

use failure;

pub trait Algorithm {
    fn verify(&self, key: &[u8], data: &[u8], expected: &[u8]) -> bool;
    fn compute(&self, key: &[u8], data: &[u8]) -> Result<Vec<u8>, failure::Error>;
}
