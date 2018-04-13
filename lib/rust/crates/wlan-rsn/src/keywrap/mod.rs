// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod aes;

use Result;

pub trait Algorithm {
    fn wrap(&self, key: &[u8], data: &[u8]) -> Result<Vec<u8>>;
    fn unwrap(&self, key: &[u8], data: &[u8]) -> Result<Vec<u8>>;
}
