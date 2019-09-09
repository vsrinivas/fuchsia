// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::ResultExt;

use fidl_fuchsia_net_stack as fidl_net_stack;

#[derive(Debug)]
pub struct NetstackError(fidl_net_stack::Error);

impl std::fmt::Display for NetstackError {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        std::fmt::Debug::fmt(self, f)
    }
}

impl std::error::Error for NetstackError {}

/// Helper trait to reduce boilerplate issuing FIDL calls.
pub trait FidlReturn {
    type Item;
    fn squash_result(self) -> Result<Self::Item, failure::Error>;
}

impl<T> FidlReturn for Result<Result<T, fidl_net_stack::Error>, fidl::Error> {
    type Item = T;

    fn squash_result(self) -> Result<Self::Item, failure::Error> {
        Ok(self.context("FIDL error")?.map_err(NetstackError).context("Netstack error")?)
    }
}
