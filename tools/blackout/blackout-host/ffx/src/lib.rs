// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    ffx_core::ffx_plugin,
    ffx_storage_blackout_step_args::{
        BlackoutCommand, BlackoutSubcommand, SetupCommand, TestCommand, VerifyCommand,
    },
    fidl::{endpoints::ProtocolMarker, prelude::*},
    fidl_fuchsia_blackout_test as fblackout,
    fidl_fuchsia_developer_remotecontrol as fremotecontrol,
    fuchsia_zircon_status::Status,
    selectors::{self, VerboseError},
};

/// Connect to a protocol on a remote device using the remote control proxy.
async fn remotecontrol_connect<S: ProtocolMarker>(
    remote_control: &fremotecontrol::RemoteControlProxy,
    selector: &str,
) -> Result<S::Proxy> {
    let (proxy, server_end) = fidl::endpoints::create_proxy::<S>()?;
    remote_control
        .connect(selectors::parse_selector::<VerboseError>(selector)?, server_end.into_channel())
        .await?
        .map_err(|e| {
            anyhow::anyhow!(
                "failed to connect to protocol {} with selector {}: {:?}",
                S::DEBUG_NAME.to_string(),
                selector.to_string(),
                e
            )
        })?;
    Ok(proxy)
}

#[ffx_plugin("storage_dev")]
async fn step(
    cmd: BlackoutCommand,
    remote_control: fremotecontrol::RemoteControlProxy,
) -> Result<()> {
    let proxy = remotecontrol_connect::<fblackout::ControllerMarker>(
        &remote_control,
        &format!(
            "core/ffx-laboratory\\:blackout-target:expose:{}",
            fblackout::ControllerMarker::PROTOCOL_NAME
        ),
    )
    .await?;

    let BlackoutCommand { step } = cmd;
    match step {
        BlackoutSubcommand::Setup(SetupCommand { device_label, device_path, seed }) => proxy
            .setup(&device_label, device_path.as_deref(), seed)
            .await?
            .map_err(|e| anyhow::anyhow!("setup failed: {}", Status::from_raw(e).to_string()))?,
        BlackoutSubcommand::Test(TestCommand { device_label, device_path, seed, duration }) => {
            proxy.test(&device_label, device_path.as_deref(), seed, duration).await?.map_err(
                |e| anyhow::anyhow!("test step failed: {}", Status::from_raw(e).to_string()),
            )?
        }
        BlackoutSubcommand::Verify(VerifyCommand { device_label, device_path, seed }) => {
            proxy.verify(&device_label, device_path.as_deref(), seed).await?.map_err(|e| {
                let status = Status::from_raw(e);
                if status == Status::BAD_STATE {
                    anyhow::anyhow!("verification failure")
                } else {
                    anyhow::anyhow!("retry-able verify step error: {}", status.to_string())
                }
            })?
        }
    };

    Ok(())
}
