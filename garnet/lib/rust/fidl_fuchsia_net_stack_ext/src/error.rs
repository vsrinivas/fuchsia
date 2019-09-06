// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::ResultExt;

use fidl_fuchsia_net_stack as fidl_net_stack;

#[derive(Debug)]
pub struct NetstackError(fidl_net_stack::ErrorType);

impl std::fmt::Display for NetstackError {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        std::fmt::Debug::fmt(self, f)
    }
}

impl std::error::Error for NetstackError {}

/// Helper trait to reduce boilerplate issuing calls to netstack FIDL.
pub trait NetstackFidlReturn {
    type Item;
    fn into_result(self) -> Result<Self::Item, NetstackError>;
}

// TODO(gongt) subject to removal once error return type clean up is done
impl<T> NetstackFidlReturn for (Option<Box<fidl_net_stack::Error>>, T) {
    type Item = T;
    fn into_result(self) -> Result<T, NetstackError> {
        match self {
            (Some(err), _) => Err(NetstackError(err.type_)),
            (None, value) => Ok(value),
        }
    }
}

// TODO(gongt) subject to removal once error return type clean up is done
impl NetstackFidlReturn for Option<Box<fidl_net_stack::Error>> {
    type Item = ();
    fn into_result(self) -> Result<(), NetstackError> {
        match self {
            Some(err) => Err(NetstackError(err.type_)),
            None => Ok(()),
        }
    }
}

impl<T> NetstackFidlReturn for Result<T, fidl_net_stack::ErrorType> {
    type Item = T;
    fn into_result(self) -> Result<Self::Item, NetstackError> {
        self.map_err(NetstackError)
    }
}

/// Helper trait to reduce boilerplate issuing FIDL calls.
pub trait FidlReturn {
    type Item;
    fn squash_result(self) -> Result<Self::Item, failure::Error>;
}

impl<R> FidlReturn for Result<R, fidl::Error>
where
    R: NetstackFidlReturn,
{
    type Item = R::Item;

    fn squash_result(self) -> Result<Self::Item, failure::Error> {
        Ok(self.context("FIDL error")?.into_result().context("Netstack error")?)
    }
}
