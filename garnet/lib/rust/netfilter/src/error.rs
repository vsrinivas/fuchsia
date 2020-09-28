// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context as _, Error};
use fidl_fuchsia_net_filter as filter;

/// Helper trait to transform the Result types returned by FIDL methods into
/// a form that's easier to work with.
pub trait FidlReturn {
    type Item;
    fn transform_result(self) -> Result<Self::Item, Error>;
}

impl FidlReturn for Result<filter::Status, fidl::Error> {
    type Item = ();
    fn transform_result(self) -> Result<(), Error> {
        let status = self.context("FIDL error")?;
        match status {
            filter::Status::Ok => Ok(()),
            _ => Err(format_err!("{:?}", status).context("Netstack error").into()),
        }
    }
}

impl<T1, T2> FidlReturn for Result<(T1, T2, filter::Status), fidl::Error> {
    type Item = (T1, T2);
    fn transform_result(self) -> Result<(T1, T2), Error> {
        let (a, b, status) = self.context("FIDL error")?;
        match status {
            filter::Status::Ok => Ok((a, b)),
            _ => Err(format_err!("{:?}", status).context("Netstack error").into()),
        }
    }
}
