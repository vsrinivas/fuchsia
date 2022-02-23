// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use ffx_core::ffx_plugin;
use ffx_setui_volume_policy_args::{SubcommandEnum, VolumePolicy};
use fidl_fuchsia_settings_policy::VolumePolicyControllerProxy;

pub use utils;

mod add;
mod get;
mod remove;

#[ffx_plugin(
    "setui",
    VolumePolicyControllerProxy = "core/setui_service:expose:fuchsia.settings.policy.VolumePolicyController"
)]
pub async fn run_command(
    proxy: VolumePolicyControllerProxy,
    volume_policy: VolumePolicy,
) -> Result<()> {
    match volume_policy.subcommand {
        SubcommandEnum::Add(args) => add::add(proxy, args).await,
        SubcommandEnum::Get(_) => get::get(proxy).await,
        SubcommandEnum::Remove(args) => remove::remove(proxy, args).await,
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::setup_fake_proxy;
    use ffx_setui_volume_policy_args::GetArgs;
    use fidl_fuchsia_media::AudioRenderUsage;
    use fidl_fuchsia_settings_policy::{
        Property, Target, Transform, VolumePolicyControllerRequest,
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_run_command() {
        const POLICY_ID: u32 = 42;
        // setup_fake_{proxy name in get()}
        let proxy = setup_fake_proxy(move |req| match req {
            VolumePolicyControllerRequest::AddPolicy { target: _, parameters: _, responder } => {
                let _ = responder.send(&mut Ok(POLICY_ID));
            }
            VolumePolicyControllerRequest::RemovePolicy { policy_id: _, responder } => {
                let _ = responder.send(&mut Ok(()));
            }
            VolumePolicyControllerRequest::GetProperties { responder } => {
                let mut properties = Vec::new();
                properties.push(Property {
                    target: Some(Target::Stream(AudioRenderUsage::Background)),
                    active_policies: Some(vec![]),
                    available_transforms: Some(vec![Transform::Max]),
                    ..Property::EMPTY
                });
                let _ = responder.send(&mut properties.into_iter());
            }
        });

        let policy = VolumePolicy { subcommand: SubcommandEnum::Get(GetArgs {}) };
        let response = run_command(proxy, policy).await;
        assert!(response.is_ok());
    }
}
