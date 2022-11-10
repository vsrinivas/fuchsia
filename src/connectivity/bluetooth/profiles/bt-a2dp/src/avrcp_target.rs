// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fuchsia_bluetooth_component::{LifecycleMarker, LifecycleProxy, LifecycleState};

/// Waits for the LifecycleState to be `Ready` or returns Error if using the `proxy` fails.
async fn lifecycle_wait_ready(proxy: LifecycleProxy) -> Result<(), Error> {
    loop {
        match proxy.get_state().await? {
            LifecycleState::Initializing => continue,
            LifecycleState::Ready => break,
        }
    }
    Ok(())
}

/// Attempt to start the AVRCP-Target component used to relay media updates to the peer.
/// Returns Ok if the AVRCP Target was successfully started, or Error otherwise.
pub async fn start_avrcp_target() -> Result<(), Error> {
    let lifecycle = fuchsia_component::client::connect_to_protocol::<LifecycleMarker>()?;
    lifecycle_wait_ready(lifecycle).await
}

#[cfg(test)]
mod tests {
    use super::*;

    use assert_matches::assert_matches;
    use async_utils::PollExt;
    use fidl_fuchsia_bluetooth_component::LifecycleRequest;
    use fuchsia_async as fasync;
    use futures::StreamExt;

    #[test]
    fn waiting_for_lifecycle_returns_when_ready() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<LifecycleMarker>().unwrap();
        let mut wait_fut = Box::pin(lifecycle_wait_ready(proxy));

        // We expect a request to get the current state of the Lifecycle service.
        // Initially respond with Initializing (e.g the child component is not ready yet.)
        let _ = exec.run_until_stalled(&mut wait_fut).expect_pending("no lifecycle response");
        match exec.run_until_stalled(&mut stream.next()).expect("lifecycle request") {
            Some(Ok(LifecycleRequest::GetState { responder, .. })) => {
                responder.send(LifecycleState::Initializing).unwrap();
            }
            x => panic!("Expected GetState request but got: {:?}", x),
        }

        // Should still be waiting. This time, we respond with Ready - future should resolve.
        let _ = exec.run_until_stalled(&mut wait_fut).expect_pending("no lifecycle response");
        match exec.run_until_stalled(&mut stream.next()).expect("lifecycle request") {
            Some(Ok(LifecycleRequest::GetState { responder, .. })) => {
                responder.send(LifecycleState::Ready).unwrap();
            }
            x => panic!("Expected GetState request but got: {:?}", x),
        }
        let result = exec.run_until_stalled(&mut wait_fut).expect("lifecycle ready response");
        assert_matches!(result, Ok(_));
    }

    #[test]
    fn waiting_for_lifecycle_returns_error_when_client_error() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<LifecycleMarker>().unwrap();
        let mut wait_fut = Box::pin(lifecycle_wait_ready(proxy));
        let _ = exec.run_until_stalled(&mut wait_fut).expect_pending("no lifecycle response");

        // Dropping the server end will result in any client requests to resolve to Error.
        drop(stream);

        let result = exec.run_until_stalled(&mut wait_fut).expect("channel closed");
        assert_matches!(result, Err(_));
    }
}
