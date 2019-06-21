// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::ast::BanjoAst, failure::Error, std::io};

pub use self::{
    abigen::AbigenBackend, ast::AstBackend, c::CBackend, cpp::CppBackend, cpp::CppSubtype,
    fidlcat::FidlcatBackend, kernel::KernelBackend, kernel::KernelSubtype,
    syzkaller::SyzkallerBackend,
};

mod abigen;
mod ast;
mod c;
mod cpp;
mod fidlcat;
mod kernel;
mod syzkaller;
mod util;

pub trait Backend<'a, W: io::Write> {
    fn codegen(&mut self, ast: BanjoAst) -> Result<(), Error>;
}
