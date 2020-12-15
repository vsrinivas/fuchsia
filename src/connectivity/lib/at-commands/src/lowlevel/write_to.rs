// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::io;

/// Trait for AT types which serialize themselves.
pub trait WriteTo {
    fn write_to<W: io::Write>(&self, sink: &mut W) -> io::Result<()>;
}
