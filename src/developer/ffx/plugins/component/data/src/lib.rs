// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    ffx_component_data_args::{DataCommand, Provider, StorageCommand},
    ffx_component_storage::storage,
    ffx_core::ffx_plugin,
    fidl_fuchsia_developer_remotecontrol::RemoteControlProxy,
};

#[ffx_plugin()]
pub async fn data(remote_proxy: RemoteControlProxy, args: DataCommand) -> Result<()> {
    storage(remote_proxy, StorageCommand { subcommand: args.subcommand, provider: Provider::Data })
        .await
}
