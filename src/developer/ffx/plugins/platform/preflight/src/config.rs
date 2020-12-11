// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[derive(Debug, PartialEq)]
#[allow(dead_code)]
pub enum OperatingSystem {
    // Mac OS parameterized by the major and minor version numbers, such as MacOS(10, 14) for MacOS 10.14.
    MacOS(u32, u32),
    // Linux.
    Linux,
}

#[derive(Debug, PartialEq)]
pub struct PreflightConfig {
    // The operating system preflight is running on.
    pub system: OperatingSystem,
}
