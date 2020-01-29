// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_developer_remotecontrol as rcs,
    fidl_fuchsia_sys::{ComponentControllerEvent, ComponentControllerEventStream},
    fuchsia_component::client::App,
    future::{self, FutureExt},
    futures::prelude::*,
};

pub struct ComponentController {
    app: App,
    request_stream: rcs::ComponentControllerRequestStream,
    control_handle: rcs::ComponentControllerControlHandle,
}

impl ComponentController {
    pub fn new(
        app: App,
        request_stream: rcs::ComponentControllerRequestStream,
        control_handle: rcs::ComponentControllerControlHandle,
    ) -> Self {
        return ComponentController { app, request_stream, control_handle };
    }

    pub async fn serve(mut self) -> Result<(), Error> {
        let events: ComponentControllerEventStream = self.app.controller().take_event_stream();
        let control_handle = self.control_handle;
        hoist::spawn(async move {
            let return_code: i64 = events
                .try_filter_map(
                    |event| -> future::Ready<std::result::Result<std::option::Option<i64>, _>> {
                        future::ready(match event {
                            ComponentControllerEvent::OnTerminated {
                                return_code,
                                termination_reason: _,
                            } => Ok(Some(return_code)),
                            _ => Ok(None),
                        })
                    },
                )
                .into_future()
                .map(|(next, _rest)| -> Result<i64, anyhow::Error> {
                    match next {
                        Some(result) => result.map_err(|err| err.into()),
                        _ => Ok(-1),
                    }
                })
                .await
                .unwrap();

            control_handle.send_on_terminated(return_code).unwrap();
            control_handle.shutdown();
        });

        while let Ok(Some(request)) = self.request_stream.try_next().await {
            match request {
                rcs::ComponentControllerRequest::Kill { responder } => match self.app.kill() {
                    Ok(()) => responder.send(true).context("sending Kill response")?,
                    Err(e) => {
                        log::warn!("error killing component: {}", e);
                        responder.send(false).context("sending Kill response")?
                    }
                },
            }
        }

        Ok(())
    }
}
