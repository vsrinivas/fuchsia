// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// The FemuBehavior is an interface for Femu-based emulators. It is meant to be implemented by the
/// Behaviors in the handlers module by any handler that should be usable by Femu, and provides
/// access methods for the data fields required by those engines. A Behavior specified in the
/// manifest should not reference this class as a handler directly.
use ffx_emulator_config::{Behavior, FemuData};

pub trait FemuBehavior {
    fn data(&self) -> Option<&FemuData>;
    fn args(&self) -> Option<Vec<String>> {
        let result = self.data();
        if let Some(result) = result {
            Some(result.args.clone())
        } else {
            None
        }
    }
    fn features(&self) -> Option<Vec<String>> {
        let result = self.data();
        if let Some(result) = result {
            Some(result.features.clone())
        } else {
            None
        }
    }
    fn kernel_args(&self) -> Option<Vec<String>> {
        let result = self.data();
        if let Some(result) = result {
            Some(result.kernel_args.clone())
        } else {
            None
        }
    }
    fn options(&self) -> Option<Vec<String>> {
        let result = self.data();
        if let Some(result) = result {
            Some(result.options.clone())
        } else {
            None
        }
    }
}

impl FemuBehavior for Behavior {
    fn data(&self) -> Option<&FemuData> {
        self.data.femu.as_ref()
    }
}
