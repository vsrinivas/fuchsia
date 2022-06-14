// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::log::AsValue;

impl<'a> AsValue<'a> for fuchsia_zircon::Status {
    type ValueType = &'a (dyn std::error::Error + 'static);

    fn as_value(&'a self) -> Self::ValueType {
        self
    }
}

impl<'a> AsValue<'a> for fidl::Error {
    type ValueType = &'a (dyn std::error::Error + 'static);

    fn as_value(&'a self) -> Self::ValueType {
        self
    }
}
