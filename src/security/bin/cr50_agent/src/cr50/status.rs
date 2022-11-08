// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_tpm_cr50::{Cr50Rc, Cr50Status};
use thiserror::Error;

#[derive(Error, Debug)]
pub enum ExecuteError {
    #[error("tpm command failed")]
    Tpm(TpmStatus),
    #[error("error while executing tpm command")]
    Other(#[from] anyhow::Error),
}

#[derive(Debug, Clone, PartialEq)]
pub struct TpmStatus(pub Cr50Rc);

impl TpmStatus {
    pub fn is_ok(&self) -> bool {
        match self.0 {
            Cr50Rc::Cr50(Cr50Status::Success) => true,
            Cr50Rc::Tpm(0) => true,
            _ => false,
        }
    }
}

impl From<TpmStatus> for u16 {
    fn from(val: TpmStatus) -> Self {
        match val.0 {
            Cr50Rc::Tpm(v) => v,
            Cr50Rc::Cr50(v) => v.into_primitive() as u16,
            _ => unimplemented!(),
        }
    }
}

impl From<u16> for TpmStatus {
    fn from(val: u16) -> Self {
        const FORMAT_SELECTOR: u16 = 1 << 7;
        const VERSION: u16 = 1 << 8;
        const VENDOR: u16 = 1 << 10;
        const V0_ERROR_MASK: u16 = (1 << 7) - 1;
        if val & (FORMAT_SELECTOR | VERSION | VENDOR) == (VERSION | VENDOR) {
            let vendor = match Cr50Status::from_primitive((val & V0_ERROR_MASK) as u8) {
                Some(value) => value,
                None => return Cr50Rc::Tpm(val).into(),
            };

            Cr50Rc::Cr50(vendor).into()
        } else {
            Cr50Rc::Tpm(val).into()
        }
    }
}

impl From<Cr50Rc> for TpmStatus {
    fn from(v: Cr50Rc) -> Self {
        Self(v)
    }
}

impl From<TpmStatus> for Cr50Rc {
    fn from(v: TpmStatus) -> Self {
        v.0
    }
}
