// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use fidl_fuchsia_settings_policy::VolumePolicyControllerProxy;
use utils::handle_mixed_result;
use utils::{self, Either, WatchOrSetResult};

pub async fn get(proxy: VolumePolicyControllerProxy) -> Result<()> {
    handle_mixed_result("VolumePolicyGet", command(proxy).await).await
}

async fn command(proxy: VolumePolicyControllerProxy) -> WatchOrSetResult {
    Ok(Either::Get(format!("{:#?}", proxy.get_properties().await)))
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::setup_fake_proxy;
    use fidl_fuchsia_media::AudioRenderUsage;
    use fidl_fuchsia_settings_policy::{
        Property, Target, Transform, VolumePolicyControllerRequest,
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get() {
        // setup_fake_{proxy name, which is defined in get()}
        let proxy = setup_fake_proxy(move |req| match req {
            VolumePolicyControllerRequest::AddPolicy { .. } => {
                panic!("Unexpected call to add policy");
            }
            VolumePolicyControllerRequest::RemovePolicy { .. } => {
                panic!("Unexpected call to remove policy");
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

        let response = get(proxy).await;
        assert!(response.is_ok());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn validate_volume_policy_get() -> Result<()> {
        // setup_fake_{proxy name, which is defined in get()}
        let proxy = setup_fake_proxy(move |req| match req {
            VolumePolicyControllerRequest::AddPolicy { .. } => {
                panic!("Unexpected call to add policy");
            }
            VolumePolicyControllerRequest::RemovePolicy { .. } => {
                panic!("Unexpected call to remove policy");
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

        let output = utils::assert_get!(command(proxy));
        // Spot-check that the output contains the available transform in the data returned from the
        // fake service.
        assert!(output.contains("Max"));
        Ok(())
    }
}
