// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_identity_account::Error as ApiError;
use fidl_fuchsia_identity_prototype::{
    PrototypeAccountTransferControlRequest, PrototypeAccountTransferControlRequestStream,
};
use futures::prelude::*;

/// An implementation of the `fuchsia.identity.prototype.PrototypeAccountTransferControl`
/// fidl protocol.  This is a temporary protocol through which an account transfer can be
/// initiated until account transfer is production ready.  `AccountTransferControl`
/// handles connecting to a target device's Account Manager and transferring account data.
pub struct AccountTransferControl;

impl AccountTransferControl {
    pub fn new() -> Self {
        AccountTransferControl {}
    }

    pub async fn handle_requests_from_stream(
        &self,
        mut stream: PrototypeAccountTransferControlRequestStream,
    ) -> Result<(), anyhow::Error> {
        while let Some(req) = stream.try_next().await? {
            self.handle_request(req).await?;
        }
        Ok(())
    }

    pub async fn handle_request(
        &self,
        req: PrototypeAccountTransferControlRequest,
    ) -> Result<(), fidl::Error> {
        let PrototypeAccountTransferControlRequest::TransferAccount { responder, .. } = req;
        responder.send(&mut Err(ApiError::UnsupportedOperation))
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_identity_account::Lifetime;
    use fidl_fuchsia_identity_prototype::{
        PrototypeAccountTransferControlMarker, PrototypeAccountTransferControlProxy,
    };
    use fidl_fuchsia_overnet_protocol::NodeId;
    use fuchsia_async as fasync;
    use futures::future::join;

    type TestResult = Result<(), anyhow::Error>;

    const TEST_ACCOUNT_ID: u64 = 0xee1u64;
    const TEST_NODE_ID: NodeId = NodeId { id: 0xbeefu64 };

    /// Creates a `PrototypeAccountTransferControlProxy` and a future which
    /// serves requests from the proxy when polled to completion.
    fn get_account_transfer_control(
    ) -> (PrototypeAccountTransferControlProxy, impl Future<Output = TestResult>) {
        let (control_proxy, control_request_stream) =
            create_proxy_and_stream::<PrototypeAccountTransferControlMarker>()
                .expect("Failed to create proxy and stream");

        let atc_future = async move {
            let atc = AccountTransferControl::new();
            atc.handle_requests_from_stream(control_request_stream).await
        };

        (control_proxy, atc_future)
    }

    #[fasync::run_until_stalled(test)]
    async fn test_transfer_account() {
        let (control_proxy, server_fut) = get_account_transfer_control();

        let test_fut = async move {
            assert_eq!(
                control_proxy
                    .transfer_account(TEST_ACCOUNT_ID, &mut TEST_NODE_ID.clone(), Lifetime::Persistent)
                    .await?,
                Err(ApiError::UnsupportedOperation)
            );
            TestResult::Ok(())
        };

        let (test_result, server_result) = join(test_fut, server_fut).await;
        assert!(test_result.is_ok());
        assert!(server_result.is_ok());
    }
}
