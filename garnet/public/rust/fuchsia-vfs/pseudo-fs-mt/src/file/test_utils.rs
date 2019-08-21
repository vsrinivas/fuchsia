// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Common utilities used by pseudo-file related tests.

use crate::{
    directory::entry::DirectoryEntry,
    test_utils::run::{self, AsyncServerClientTestParams},
};

use {
    fidl_fuchsia_io::{FileMarker, FileProxy},
    futures::Future,
    std::sync::Arc,
};

pub use run::{run_client, test_client};

/// A thin wrapper around [`test_utils::run::run_server_client`] that sets the `Marker` to be
/// [`FileMarker`], and providing explicit type for the `get_client` closure argument.  This makes
/// it possible for the caller not to provide explicit types.
pub fn run_server_client<GetClientRes>(
    flags: u32,
    server: Arc<dyn DirectoryEntry>,
    get_client: impl FnOnce(FileProxy) -> GetClientRes,
) where
    GetClientRes: Future<Output = ()>,
{
    run::run_server_client::<FileMarker, _, _>(flags, server, get_client)
}

/// A thin wrapper around [`test_utils::run::test_server_client`] that sets the `Marker` to be
/// [`FileMarker`], and providing explicit type for the `get_client` closure argument.  This makes
/// it possible for the caller not to provide explicit types.
pub fn test_server_client<'test_refs, GetClientRes>(
    flags: u32,
    server: Arc<dyn DirectoryEntry>,
    get_client: impl FnOnce(FileProxy) -> GetClientRes + 'test_refs,
) -> AsyncServerClientTestParams<'test_refs, FileMarker>
where
    GetClientRes: Future<Output = ()> + 'test_refs,
{
    run::test_server_client::<FileMarker, _, _>(flags, server, get_client)
}
