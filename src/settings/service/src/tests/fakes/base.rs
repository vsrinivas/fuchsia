// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::handler::base::GenerateHandler;
use crate::handler::base::Request;
use crate::handler::setting_handler::{reply, Command, Payload, SettingHandlerResult, State};
use anyhow::Error;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::future::BoxFuture;
use futures::lock::Mutex;
use std::sync::Arc;

/// Trait for providing a service.
pub(crate) trait Service {
    /// Returns true if this service can process the given service name, false
    /// otherwise.
    fn can_handle_service(&self, service_name: &str) -> bool;

    /// Processes the request stream within the specified channel. Ok is returned
    /// on success, an error otherwise.
    fn process_stream(&mut self, service_name: &str, channel: zx::Channel) -> Result<(), Error>;
}

/// A helper function for creating a simple setting handler.
pub(crate) fn create_setting_handler(
    request_handler: Box<
        dyn Fn(Request) -> BoxFuture<'static, SettingHandlerResult> + Send + Sync + 'static,
    >,
) -> GenerateHandler {
    let shared_handler = Arc::new(Mutex::new(request_handler));
    Box::new(move |mut context| {
        let handler = shared_handler.clone();
        fasync::Task::spawn(async move {
            while let Ok((payload, client)) = context.receptor.next_of::<Payload>().await {
                // There could be other events such as acks so do not necessarily
                // return an error if a different message event is received here.
                match payload {
                    Payload::Command(Command::HandleRequest(request)) => {
                        let response = (handler.lock().await)(request).await;
                        reply(client, response);
                    }
                    Payload::Command(Command::ChangeState(state)) => {
                        if state == State::Startup || state == State::Teardown {
                            reply(client, Ok(None));
                        }
                    }
                    _ => {
                        // Ignore other payloads
                    }
                }
            }
        })
        .detach();

        Box::pin(async move { Ok(()) })
    })
}
