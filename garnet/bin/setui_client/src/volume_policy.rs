// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::VolumePolicyCommands;
use anyhow::{format_err, Error};
use fidl_fuchsia_settings_policy::{
    Disable, Mute, PolicyParameters, Target, Volume, VolumePolicyControllerProxy,
};

pub async fn command(
    proxy: VolumePolicyControllerProxy,
    add: Option<VolumePolicyCommands>,
    remove: Option<u32>,
) -> Result<String, Error> {
    if let Some(VolumePolicyCommands::AddPolicy(add_options)) = add {
        let mut add_results = Vec::new();
        if let Some(min_volume) = add_options.min {
            add_results.push(
                add_policy_request(
                    &proxy,
                    add_options.target,
                    PolicyParameters::Min(Volume { volume: Some(min_volume), ..Volume::EMPTY }),
                )
                .await,
            );
        }
        if let Some(max_volume) = add_options.max {
            add_results.push(
                add_policy_request(
                    &proxy,
                    add_options.target,
                    PolicyParameters::Max(Volume { volume: Some(max_volume), ..Volume::EMPTY }),
                )
                .await,
            );
        }
        if let Some(mute) = add_options.mute {
            add_results.push(
                add_policy_request(
                    &proxy,
                    add_options.target,
                    PolicyParameters::Mute(Mute { mute: Some(mute), ..Mute::EMPTY }),
                )
                .await,
            );
        }
        if let Some(true) = add_options.disable {
            add_results.push(
                add_policy_request(
                    &proxy,
                    add_options.target,
                    PolicyParameters::Disable(Disable { ..Disable::EMPTY }),
                )
                .await,
            );
        }
        if add_results.is_empty() {
            Err(format_err!("No policies specified"))
        } else {
            Ok(add_results.join("\n"))
        }
    } else if let Some(policy_id) = remove {
        let remove_result = proxy.remove_policy(policy_id).await?;
        match remove_result {
            Ok(_) => Ok(format!("Successfully removed {:?}", policy_id)),
            Err(err) => Ok(format!("{:#?}", err)),
        }
    } else {
        // No values set, perform a get instead.
        let setting_value = proxy.get_properties().await?;
        Ok(format!("{:#?}", setting_value))
    }
}

/// Perform an add policy request and return the result, formatted as a string.
async fn add_policy_request(
    proxy: &VolumePolicyControllerProxy,
    target: fidl_fuchsia_media::AudioRenderUsage,
    parameters: PolicyParameters,
) -> String {
    let add_result = proxy.add_policy(&mut Target::Stream(target), &mut parameters.clone()).await;
    match add_result {
        Ok(policy_id) => {
            format!("Added parameters '{:?}'; policy id is {:?}", parameters, policy_id.unwrap())
        }
        Err(err) => format!("{:#?}", err),
    }
}
