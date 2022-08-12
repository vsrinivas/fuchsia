// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_net_filter as fnet_filter;
use futures::TryStreamExt;
use log::error;

pub(crate) async fn serve(stream: fnet_filter::FilterRequestStream) -> Result<(), fidl::Error> {
    use fnet_filter::FilterRequest;

    stream
        .try_for_each(|request: FilterRequest| async move {
            // TODO(https://fxbug.dev/106604): implement filtering support.

            match request {
                FilterRequest::DisableInterface { responder, .. } => {
                    error!(
                        "fuchsia.net.filter.Filter is not implemented \
                           (https://fxbug.dev/106604); ignoring DisableInterface"
                    );
                    responder_send!(responder, &mut Ok(()));
                }
                FilterRequest::EnableInterface { .. } => {
                    todo!("https://fxbug.dev/106604: implement filtering support");
                }
                FilterRequest::GetRules { responder } => {
                    error!(
                        "fuchsia.net.filter.Filter is not implemented \
                           (https://fxbug.dev/106604); ignoring GetRules"
                    );
                    responder_send!(responder, &mut Ok((vec![], 0)));
                }
                FilterRequest::UpdateRules { rules, generation, responder } => {
                    error!(
                        "fuchsia.net.filter.Filter is not implemented \
                            (https://fxbug.dev/106604); ignoring UpdateRules \
                            {{ generation: {:?}, rules: {:?} }}",
                        generation, rules
                    );
                    responder_send!(responder, &mut Ok(()));
                }
                FilterRequest::GetNatRules { .. } => {
                    todo!("https://fxbug.dev/106604: implement filtering support");
                }
                FilterRequest::UpdateNatRules { .. } => {
                    todo!("https://fxbug.dev/106604: implement filtering support");
                }
                FilterRequest::GetRdrRules { .. } => {
                    todo!("https://fxbug.dev/106604: implement filtering support");
                }
                FilterRequest::UpdateRdrRules { .. } => {
                    todo!("https://fxbug.dev/106604: implement filtering support");
                }
            };
            Ok(())
        })
        .await
}
