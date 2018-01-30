// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![feature(test)]

#[macro_use]
extern crate failure;
#[macro_use]
extern crate nom;
extern crate test;

pub mod rsne;
mod integrity;
mod keywrap;
mod suite_selector;
mod cipher;
mod akm;
mod pmkid;

use std::result;

pub type Result<T> = result::Result<T, Error>;

#[derive(Debug, Fail)]
pub enum Error {
    #[fail(display = "invalid OUI length; expected 3 bytes but received {}", _0)]
    InvalidOuiLength(usize),
    #[fail(display = "invalid PMKID length; expected 16 bytes but received {}", _0)]
    InvalidPmkidLength(usize),
}