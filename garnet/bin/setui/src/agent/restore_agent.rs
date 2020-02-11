// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// The Restore Agent is responsible for signaling to all components to restore
/// external sources to the last known value. It is invoked during startup.
use crate::agent::base::{Agent, Invocation, Lifespan};
use crate::switchboard::base::{SettingRequest, SettingResponse};
use anyhow::Error;
use fuchsia_async as fasync;
use fuchsia_syslog::fx_log_err;

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
        if invocation.context.lifespan != Lifespan::Initialization {
            return Ok(false);
        }

        fasync::spawn(async move {
            for component in invocation.context.clone().available_components {
                let (result_tx, result_rx) =
                    futures::channel::oneshot::channel::<Result<Option<SettingResponse>, Error>>();
                let switchboard = invocation.context.switchboard.clone();
                if switchboard
                    .lock()
                    .await
                    .request(component, SettingRequest::Restore, result_tx)
                    .is_ok()
                {
                    if result_rx.await.is_err() {
                        fx_log_err!("could not restore the following setting:{:?}", component);
                    }
                } else {
                    reply(
                        invocation,
                        Err(anyhow::format_err!("could not request restore from component")),
                    )
                    .await;
                    return;
                }
            }

            reply(invocation, Ok(())).await;
        });

        return Ok(true);
    }
}
