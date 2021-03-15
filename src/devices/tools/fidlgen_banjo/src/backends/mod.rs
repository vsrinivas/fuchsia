// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::fidl::FidlIr, anyhow::Error, std::io};

pub use self::c::CBackend;
pub use self::cpp::CppBackend;
pub use self::cpp_internal::CppInternalBackend;
pub use self::cpp_mock::CppMockBackend;
pub use self::rust::RustBackend;

mod c;
mod cpp;
mod cpp_internal;
mod cpp_mock;
mod rust;
mod util;

pub trait Backend<'a, W: io::Write> {
    fn codegen(&mut self, ir: FidlIr) -> Result<(), Error>;
}
