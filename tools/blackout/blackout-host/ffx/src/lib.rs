// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    ffx_core::ffx_plugin,
    ffx_storage_blackout_step_args::{
        BlackoutCommand, BlackoutSubcommand, SetupCommand, TestCommand, VerifyCommand,
    },
    fidl::endpoints::ProtocolMarker,
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
    let _: fremotecontrol::ServiceMatch = remote_control
        .connect(selectors::parse_selector::<VerboseError>(selector)?, server_end.into_channel())
        .await?
        .map_err(|e| {
            anyhow::anyhow!(
                "failed to connect to protocol {} with selector {}: {:?}",
                S::NAME.to_string(),
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
            fblackout::ControllerMarker::NAME
        ),
    )
    .await?;

    let BlackoutCommand { step } = cmd;
    match step {
        BlackoutSubcommand::Setup(SetupCommand { block_device, seed }) => proxy
            .setup(&block_device, seed)
            .await?
            .map_err(|e| anyhow::anyhow!("setup failed: {}", Status::from_raw(e).to_string()))?,
        BlackoutSubcommand::Test(TestCommand { block_device, seed }) => {
            proxy.test(&block_device, seed)?
        }
        BlackoutSubcommand::Verify(VerifyCommand { block_device, seed }) => {
            proxy.verify(&block_device, seed).await?.map_err(|e| {
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
