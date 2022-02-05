// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_component as fcomponent;
use fidl_fuchsia_component_runner as fcrunner;
use fidl_fuchsia_process as fprocess;
use fidl_fuchsia_starnix_developer as fstardev;
use fuchsia_async as fasync;
use fuchsia_runtime::{HandleInfo, HandleType};
use fuchsia_zircon::HandleBased;
use futures::TryStreamExt;
use log::error;

use super::*;

pub async fn serve_starnix_manager(
    mut request_stream: fstardev::ManagerRequestStream,
) -> Result<(), Error> {
    while let Some(event) = request_stream.try_next().await? {
        match event {
            fstardev::ManagerRequest::Start { url, responder } => {
                let args = fcomponent::CreateChildArgs {
                    numbered_handles: None,
                    ..fcomponent::CreateChildArgs::EMPTY
                };
                if let Err(e) = create_child_component(url, args).await {
                    error!("failed to create child component: {}", e);
                }
                responder.send()?;
            }
            fstardev::ManagerRequest::StartShell { params, controller, .. } => {
                start_shell(params, controller).await?;
            }
        }
    }
    Ok(())
}

pub async fn serve_component_runner(
    mut request_stream: fcrunner::ComponentRunnerRequestStream,
) -> Result<(), Error> {
    while let Some(event) = request_stream.try_next().await? {
        match event {
            fcrunner::ComponentRunnerRequest::Start { start_info, controller, .. } => {
                fasync::Task::local(async move {
                    if let Err(e) = start_component(start_info, controller) {
                        error!("failed to start component: {:?}", e);
                    }
                })
                .detach();
            }
        }
    }
    Ok(())
}

async fn start_shell(
    params: fstardev::ShellParams,
    controller: ServerEnd<fstardev::ShellControllerMarker>,
) -> Result<(), Error> {
    let controller_handle_info = fprocess::HandleInfo {
        handle: controller.into_channel().into_handle(),
        id: HandleInfo::new(HandleType::User0, 0).as_raw(),
    };
    let numbered_handles = vec![
        handle_info_from_socket(params.standard_in, 0)?,
        handle_info_from_socket(params.standard_out, 1)?,
        handle_info_from_socket(params.standard_err, 2)?,
        controller_handle_info,
    ];
    let args = fcomponent::CreateChildArgs {
        numbered_handles: Some(numbered_handles),
        ..fcomponent::CreateChildArgs::EMPTY
    };

    create_child_component(
        "fuchsia-pkg://fuchsia.com/test_android_distro#meta/sh.cm".to_string(),
        args,
    )
    .await
}
