// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This struct contains all of the transient state that can
/// be kept between invocations of commands when `lowpanctl` is
/// invoked in interactive mode. For single command execution
/// it is set up once and then discarded.
#[derive(PartialEq, Debug)]
pub struct LowpanCtlContext {}

impl LowpanCtlContext {
    pub fn new() -> LowpanCtlContext {
        LowpanCtlContext {}
    }
}
