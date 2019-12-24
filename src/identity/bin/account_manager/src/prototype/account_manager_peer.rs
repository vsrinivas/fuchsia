// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_identity_transfer::{AccountManagerPeerRequest, AccountManagerPeerRequestStream};
use futures::prelude::*;

/// An implementation of the server end of the
/// `fuchsia.identity.transfer.AccountManagerPeer` interface that serves
/// connections from Account Managers on remote devices.
pub struct AccountManagerPeer;

impl AccountManagerPeer {
    pub fn new() -> Self {
        AccountManagerPeer {}
    }

    pub async fn handle_requests_from_stream(
        &self,
        mut stream: AccountManagerPeerRequestStream,
    ) -> Result<(), anyhow::Error> {
        while let Some(req) = stream.try_next().await? {
            self.handle_request(req).await?;
        }
        Ok(())
    }

    pub async fn handle_request(&self, req: AccountManagerPeerRequest) -> Result<(), fidl::Error> {
        let AccountManagerPeerRequest::ReceiveAccount { account_transfer, .. } = req;
        // Since ReceiveAccount doesn't have a response, just drop the AccountTransfer
        // channel instead to indicate unimplemented.
        std::mem::drop(account_transfer);
        Ok(())
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fidl::endpoints::{create_proxy, create_proxy_and_stream};
    use fidl_fuchsia_identity_account::Lifetime;
    use fidl_fuchsia_identity_transfer::{
        AccountManagerPeerMarker, AccountManagerPeerProxy, AccountTransferMarker,
    };
    use fuchsia_async as fasync;
    use futures::future::join;

    type TestResult = Result<(), anyhow::Error>;

    /// Creates an `AccountManagerPeerProxy` and a future which serves requests
    /// from the proxy when polled to completion.
    fn get_account_manager_peer() -> (AccountManagerPeerProxy, impl Future<Output = TestResult>) {
        let (peer_proxy, peer_request_stream) =
            create_proxy_and_stream::<AccountManagerPeerMarker>()
                .expect("Failed to create proxy and stream");

        let account_manager_peer = AccountManagerPeer::new();
        let server_fut = async move {
            account_manager_peer.handle_requests_from_stream(peer_request_stream).await
        };

        (peer_proxy, server_fut)
    }

    #[fasync::run_until_stalled(test)]
    async fn test_receive_account() {
        let (peer_proxy, server_fut) = get_account_manager_peer();

        let test_fut = async move {
            let (account_transfer_proxy, account_transfer_server) =
                create_proxy::<AccountTransferMarker>()?;

            peer_proxy.receive_account(Lifetime::Ephemeral, account_transfer_server)?;

            // Normal usage of account transfer is to wait for the OnTransferReady
            // event from the server side.  Since the handle is dropped on the server
            // side, we expect to get None when polling the event stream.
            let mut event_stream = account_transfer_proxy.take_event_stream();
            assert!(event_stream.try_next().await?.is_none());
            TestResult::Ok(())
        };

        let (test_result, server_result) = join(test_fut, server_fut).await;
        assert!(test_result.is_ok());
        assert!(server_result.is_ok());
    }
}
