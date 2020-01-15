// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Helper methods for the `NodeProxy` objects.

use {
    fidl::endpoints::{create_proxy, ServerEnd, ServiceMarker},
    fidl_fuchsia_io::{DirectoryProxy, FileProxy, NodeMarker, NodeProxy},
};

pub fn open_get_proxy<M>(proxy: &DirectoryProxy, flags: u32, mode: u32, path: &str) -> M::Proxy
where
    M: ServiceMarker,
{
    let (new_proxy, new_server_end) =
        create_proxy::<M>().expect("Failed to create connection endpoints");

    proxy
        .open(flags, mode, path, ServerEnd::<NodeMarker>::new(new_server_end.into_channel()))
        .unwrap();

    new_proxy
}

/// This trait repeats parts of the `NodeProxy` trait, and is implemented for `NodeProxy`,
/// `FileProxy`, and `DirectoryProxy`, which all share the same API.  FIDL currently does not
/// expose the API inheritance, so with this trait we have a workaround.  As soon as FIDL will
/// export the necessary information we should remove this trait, as it is just a workaround. The
/// downsides is that this mapping needs to be manually updated, and that it is not something all
/// the users of FIDL of `NodeProxy` would be familiar with - unnecessary complexity.
///
/// Add methods to this trait when they are necessary to reduce the maintenance effort.
pub trait NodeProxyApi {
    fn clone(&self, flags: u32, server_end: ServerEnd<NodeMarker>) -> Result<(), fidl::Error>;
}

/// Calls .clone() on the proxy object, and returns a client side of the connection passed into the
/// clone() method.
pub fn clone_get_proxy<M, Proxy>(proxy: &Proxy, flags: u32) -> M::Proxy
where
    M: ServiceMarker,
    Proxy: NodeProxyApi,
{
    let (new_proxy, new_server_end) =
        create_proxy::<M>().expect("Failed to create connection endpoints");

    proxy.clone(flags, new_server_end.into_channel().into()).unwrap();

    new_proxy
}

impl NodeProxyApi for NodeProxy {
    fn clone(&self, flags: u32, server_end: ServerEnd<NodeMarker>) -> Result<(), fidl::Error> {
        NodeProxy::clone(self, flags, server_end)
    }
}

impl NodeProxyApi for FileProxy {
    fn clone(&self, flags: u32, server_end: ServerEnd<NodeMarker>) -> Result<(), fidl::Error> {
        FileProxy::clone(self, flags, server_end)
    }
}

impl NodeProxyApi for DirectoryProxy {
    fn clone(&self, flags: u32, server_end: ServerEnd<NodeMarker>) -> Result<(), fidl::Error> {
        DirectoryProxy::clone(self, flags, server_end)
    }
}
