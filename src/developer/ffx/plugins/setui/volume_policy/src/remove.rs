// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use ffx_setui_volume_policy_args::RemoveArgs;
use fidl_fuchsia_settings_policy::VolumePolicyControllerProxy;
use utils::handle_mixed_result;
use utils::{self, Either, WatchOrSetResult};

pub async fn remove(proxy: VolumePolicyControllerProxy, args: RemoveArgs) -> Result<()> {
    handle_mixed_result("VolumePolicyRemove", command(proxy, args).await).await
}

async fn command(proxy: VolumePolicyControllerProxy, args: RemoveArgs) -> WatchOrSetResult {
    let remove_result = proxy.remove_policy(args.policy_id).await?;
    Ok(Either::Set(match remove_result {
        Ok(_) => format!("Successfully removed {:?}", args.policy_id),
        Err(err) => format!("{:#?}", err),
    }))
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::setup_fake_proxy;
    use fidl_fuchsia_settings_policy::VolumePolicyControllerRequest;
    use test_case::test_case;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_remove() {
        const POLICY_ID: u32 = 42;
        // setup_fake_{proxy name, which is defined in remove()}
        let proxy = setup_fake_proxy(move |req| match req {
            VolumePolicyControllerRequest::AddPolicy { .. } => {
                panic!("Unexpected call to add policy");
            }
            VolumePolicyControllerRequest::RemovePolicy { responder, .. } => {
                let _ = responder.send(&mut Ok(()));
            }
            VolumePolicyControllerRequest::GetProperties { .. } => {
                panic!("Unexpected call to get policy");
            }
        });

        let args = RemoveArgs { policy_id: POLICY_ID };
        let response = remove(proxy, args).await;
        assert!(response.is_ok());
    }

    #[test_case(
        RemoveArgs {
            policy_id: 42
        };
        "Test removing a policy sends the proper call to the volume policy API."
    )]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn validate_volume_policy_remove(expected_remove: RemoveArgs) -> Result<()> {
        let remove_clone = expected_remove.clone();
        // setup_fake_{proxy name, which is defined in remove()}
        let proxy = setup_fake_proxy(move |req| match req {
            VolumePolicyControllerRequest::AddPolicy { .. } => {
                panic!("Unexpected call to add policy");
            }
            VolumePolicyControllerRequest::RemovePolicy { policy_id, responder } => {
                assert_eq!(policy_id, expected_remove.policy_id);
                let _ = responder.send(&mut Ok(()));
            }
            VolumePolicyControllerRequest::GetProperties { .. } => {
                panic!("Unexpected call to get properties");
            }
        });

        // Attempt to remove the given policy ID.
        utils::assert_set!(command(proxy, remove_clone));
        Ok(())
    }
}
