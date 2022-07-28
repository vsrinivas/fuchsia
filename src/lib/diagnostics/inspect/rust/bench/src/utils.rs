// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_inspect::{TreeMarker, TreeProxy};
use fuchsia_inspect::Inspector;
use futures::{future::BoxFuture, FutureExt};
use inspect_runtime::service::{handle_request_stream, TreeServerSettings};

/// Spawns a tree server for the test purposes.
pub fn spawn_server(
    inspector: Inspector,
) -> Result<(TreeProxy, BoxFuture<'static, Result<(), anyhow::Error>>), anyhow::Error> {
    let (tree, request_stream) = fidl::endpoints::create_proxy_and_stream::<TreeMarker>()?;
    let tree_server_fut =
        handle_request_stream(inspector, TreeServerSettings::default(), request_stream);
    Ok((tree, tree_server_fut.boxed()))
}
