// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fuchsia_bluetooth_sys::{ConfigurationRequest, ConfigurationRequestStream};
use futures::stream::StreamExt;

use crate::host_dispatcher::HostDispatcher;
/// Start processing Configuration FIDL protocol messages from the given |stream|.
pub async fn run(hd: HostDispatcher, mut stream: ConfigurationRequestStream) -> Result<(), Error> {
    while let Some(event) = stream.next().await {
        handler(hd.clone(), event?).await?;
    }
    Ok(())
}

async fn handler(hd: HostDispatcher, event: ConfigurationRequest) -> fidl::Result<()> {
    match event {
        ConfigurationRequest::Update { settings, responder } => {
            let new_config = hd.apply_sys_settings(settings).await;
            responder.send(new_config.into())
        }
    }
}
