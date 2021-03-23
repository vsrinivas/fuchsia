// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {moniker::AbsoluteMoniker, thiserror::Error};

/// Errors produced by `ComponentInstanceInterface`.
#[derive(Debug, Error, Clone)]
pub enum ComponentInstanceError {
    #[error("component instance {} not found", moniker)]
    InstanceNotFound { moniker: AbsoluteMoniker },
    #[error("component manager instance unavailable")]
    ComponentManagerInstanceUnavailable {},
}

impl ComponentInstanceError {
    pub fn instance_not_found(moniker: AbsoluteMoniker) -> ComponentInstanceError {
        ComponentInstanceError::InstanceNotFound { moniker }
    }

    pub fn cm_instance_unavailable() -> ComponentInstanceError {
        ComponentInstanceError::ComponentManagerInstanceUnavailable {}
    }
}
