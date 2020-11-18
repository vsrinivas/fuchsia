// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use fidl::endpoints::ClientEnd;
use fuchsia_async as fasync;
use futures::TryStreamExt;
use tracing::error;

use fidl_fuchsia_logger::{
    LogListenerMarker, LogListenerProxy, LogListenerSafeMarker, LogListenerSafeRequest,
};

/// Wraps a legacy LogListener by spawning a task to proxy its messages to a LogListenerSafe.
/// Returns the proxied channel.
pub fn pretend_scary_listener_is_safe(
    listener: ClientEnd<LogListenerMarker>,
) -> Result<ClientEnd<LogListenerSafeMarker>, fidl::Error> {
    let (new_client, new_server) = fidl::endpoints::create_endpoints::<LogListenerSafeMarker>()?;
    let mut new_requests = new_server.into_stream()?;
    let mut old_listener = listener.into_proxy()?;

    fasync::Task::spawn(async move {
        while let Ok(Some(request)) = new_requests.try_next().await {
            if let Err(error) = bridge_request(&mut old_listener, request) {
                error!(?error, "error bridging old LogListener to LogListenerSafe");
                break;
            }
        }
    })
    .detach();

    Ok(new_client)
}

fn bridge_request(
    listener: &mut LogListenerProxy,
    request: LogListenerSafeRequest,
) -> Result<(), fidl::Error> {
    match request {
        LogListenerSafeRequest::Log { mut log, responder } => {
            listener.log(&mut log)?;
            responder.send()?;
        }
        LogListenerSafeRequest::LogMany { mut log, responder } => {
            listener.log_many(&mut log.iter_mut())?;
            responder.send()?;
        }
        LogListenerSafeRequest::Done { control_handle } => {
            listener.done()?;
            control_handle.shutdown();
        }
    }

    Ok(())
}
