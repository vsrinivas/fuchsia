// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[derive(Debug, Default, Clone, Copy)]
pub struct IndicatorStatus {
    pub service: bool,
    pub call: bool,
    pub callsetup: (),
    pub callheld: (),
    pub signal: u8,
    pub roam: bool,
    pub battchg: u8,
}
