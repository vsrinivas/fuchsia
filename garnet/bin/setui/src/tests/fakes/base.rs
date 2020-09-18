// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::handler::base::Command;
use crate::handler::base::{GenerateHandler, SettingHandlerResult, State};
use crate::handler::device_storage::DeviceStorageFactory;
use crate::internal::handler::{reply, Payload};
use crate::message::base::MessageEvent;
use crate::switchboard::base::SettingRequest;
use anyhow::Error;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::future::BoxFuture;
use futures::lock::Mutex;
use futures::StreamExt;
use std::sync::Arc;

/// Trait for providing a service.
pub trait Service {
    /// Returns true if this service can process the given service name, false
    /// otherwise.
    fn can_handle_service(&self, service_name: &str) -> bool;

    /// Processes the request stream within the specified channel. Ok is returned
    /// on success, an error otherwise.
    fn process_stream(&mut self, service_name: &str, channel: zx::Channel) -> Result<(), Error>;
}

/// A helper function for creating a simple setting handler.
pub fn create_setting_handler<T: DeviceStorageFactory + Send + Sync + 'static>(
    request_handler: Box<
        dyn Fn(SettingRequest) -> BoxFuture<'static, SettingHandlerResult> + Send + Sync + 'static,
    >,
) -> GenerateHandler<T> {
    let shared_handler = Arc::new(Mutex::new(request_handler));
    return Box::new(move |mut context| {
        let handler = shared_handler.clone();
        fasync::Task::spawn(async move {
            while let Some(event) = context.receptor.next().await {
                match event {
                    MessageEvent::Message(
                        Payload::Command(Command::HandleRequest(request)),
                        client,
                    ) => {
                        let response = (handler.lock().await)(request).await;
                        reply(client, response);
                    }
                    MessageEvent::Message(
                        Payload::Command(Command::ChangeState(state)),
                        client,
                    ) => {
                        if state == State::Startup || state == State::Teardown {
                            reply(client, Ok(None));
                        }
                    }
                    _ => {}
                }
            }
        })
        .detach();

        Box::pin(async move { Ok(()) })
    });
}
