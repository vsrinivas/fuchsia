// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// The Restore Agent is responsible for signaling to all components to restore
/// external sources to the last known value. It is invoked during startup.
use crate::agent::base::{Agent, Invocation, Lifespan};
use crate::switchboard::base::{SettingRequest, SwitchboardError};
use anyhow::Error;
use fuchsia_async as fasync;

#[derive(Debug)]
pub struct RestoreAgent;

impl RestoreAgent {
    pub fn new() -> RestoreAgent {
        RestoreAgent {}
    }
}

async fn reply(invocation: Invocation, result: Result<(), Error>) {
    if let Some(sender) = invocation.ack_sender.lock().await.take() {
        sender.send(result).ok();
    }
}

impl Agent for RestoreAgent {
    fn invoke(&mut self, invocation: Invocation) -> Result<bool, Error> {
        // Only process initialization lifespans.
        if let Lifespan::Initialization(context) = invocation.lifespan.clone() {
            fasync::spawn(async move {
                for component in context.available_components {
                    if let Ok(result_rx) =
                        context.switchboard_client.request(component, SettingRequest::Restore).await
                    {
                        if let Ok(result) = result_rx.await {
                            if result.is_ok() {
                                continue;
                            }

                            if let Err(SwitchboardError::UnimplementedRequest {
                                setting_type: _,
                                request: _,
                            }) = result
                            {
                                continue;
                            }
                        }
                    }

                    reply(
                        invocation,
                        Err(anyhow::format_err!("could not request restore from component")),
                    )
                    .await;
                    return;
                }

                reply(invocation, Ok(())).await;
            });

            return Ok(true);
        } else {
            return Ok(false);
        }
    }
}
