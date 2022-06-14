// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub use tracing::{debug, error, info, warn};

pub trait AsValue<'a> {
    type ValueType;

    fn as_value(&'a self) -> Self::ValueType;
}

impl<'a> AsValue<'a> for anyhow::Error {
    type ValueType = &'a (dyn std::error::Error + 'static);

    fn as_value(&'a self) -> Self::ValueType {
        self.as_ref()
    }
}

impl<'a> AsValue<'a> for fidl::Error {
    type ValueType = &'a (dyn std::error::Error + 'static);

    fn as_value(&'a self) -> Self::ValueType {
        self
    }
}
