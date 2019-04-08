// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::protocol::response::Response;

/// Description of the work that needs to be done to perform the update, based
/// on the Omaha response.  This is completely platform-specific.
pub trait InstallPlan: std::marker::Sized {
    fn from_response(resp: &Response) -> Option<Self>;
}

#[cfg(test)]
pub struct StubInstallPlan;

#[cfg(test)]
impl InstallPlan for StubInstallPlan {
    fn from_response(_resp: &Response) -> Option<Self> {
        Some(StubInstallPlan)
    }
}
