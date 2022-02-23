// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::format_err;
use anyhow::Result;
use ffx_setui_volume_policy_args::AddArgs;
use fidl_fuchsia_settings_policy::{PolicyParameters, Target, Volume, VolumePolicyControllerProxy};
use utils::handle_mixed_result;
use utils::{self, Either, WatchOrSetResult};

pub async fn add(proxy: VolumePolicyControllerProxy, args: AddArgs) -> Result<()> {
    handle_mixed_result("VolumePolicyAdd", command(proxy, args).await).await
}

async fn command(proxy: VolumePolicyControllerProxy, add_options: AddArgs) -> WatchOrSetResult {
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
    if add_results.is_empty() {
        Err(format_err!("No policies specified"))
    } else {
        Ok(Either::Set(add_results.join("\n")))
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
            format!(
                "Added parameters '{:?}'; policy id is {:?}",
                parameters,
                policy_id.expect("invalid policy_id received in the add policy request.")
            )
        }
        Err(err) => format!("{:#?}", err),
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::setup_fake_proxy;
    use fidl_fuchsia_media::AudioRenderUsage;
    use fidl_fuchsia_settings_policy::VolumePolicyControllerRequest;
    use test_case::test_case;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add() {
        const POLICY_ID: u32 = 42;
        // setup_fake_{proxy name, which is defined in add()}
        let proxy = setup_fake_proxy(move |req| match req {
            VolumePolicyControllerRequest::AddPolicy { responder, .. } => {
                let _ = responder.send(&mut Ok(POLICY_ID));
            }
            VolumePolicyControllerRequest::RemovePolicy { .. } => {
                panic!("Unexpected call to remove policy");
            }
            VolumePolicyControllerRequest::GetProperties { .. } => {
                panic!("Unexpected call to get policy");
            }
        });

        let args = AddArgs { target: AudioRenderUsage::Media, min: None, max: Some(0.5) };
        let response = add(proxy, args).await;
        assert!(response.is_ok());
    }

    #[test_case(
        AddArgs {
            target: AudioRenderUsage::Background,
            min: None,
            max: Some(1.0),
        };
        "Test adding a new policy with target as AudioRenderUsage::Background, max volume as 1.0, \
        works and prints out the resulting policy ID."
    )]
    #[test_case(
        AddArgs {
            target: AudioRenderUsage::Background,
            min: Some(0.5),
            max: None,
        };
        "Test adding a new policy with target as AudioRenderUsage::Background, min volume as 0.5, \
        works and prints out the resulting policy ID."
    )]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn validate_volume_policy_add(expected_add: AddArgs) -> Result<()> {
        const POLICY_ID: u32 = 42;
        let add_clone = expected_add.clone();
        // setup_fake_{proxy name, which is defined in add()}
        let proxy = setup_fake_proxy(move |req| match req {
            VolumePolicyControllerRequest::AddPolicy { target, parameters, responder } => {
                assert_eq!(target, Target::Stream(expected_add.target));
                assert_eq!(
                    parameters,
                    if expected_add.max.is_some() {
                        PolicyParameters::Max(Volume { volume: expected_add.max, ..Volume::EMPTY })
                    } else {
                        PolicyParameters::Min(Volume { volume: expected_add.min, ..Volume::EMPTY })
                    }
                );
                let _ = responder.send(&mut Ok(POLICY_ID));
            }
            VolumePolicyControllerRequest::RemovePolicy { .. } => {
                panic!("Unexpected call to remove policy");
            }
            VolumePolicyControllerRequest::GetProperties { .. } => {
                panic!("Unexpected call to get policy");
            }
        });

        let output = utils::assert_set!(command(proxy, add_clone));
        // Verify that the output contains the policy ID returned from the fake proxy.
        assert!(output.contains(POLICY_ID.to_string().as_str()));
        Ok(())
    }
}
