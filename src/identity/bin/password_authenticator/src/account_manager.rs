// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_identity_account::{
    AccountManagerRequest::{
        self, DeprecatedGetAccount, DeprecatedProvisionNewAccount, GetAccount,
        GetAccountAuthStates, GetAccountIds, GetAccountMetadata, GetAuthenticationMechanisms,
        ProvisionNewAccount, RegisterAccountListener, RemoveAccount,
    },
    AccountManagerRequestStream, Error,
};
use futures::prelude::*;
use futures::TryStreamExt;
use log::warn;

// TODO(zarvox): retain binding of open channels to accounts, once account handles are implemented
pub struct AccountManager {}

impl AccountManager {
    pub async fn handle_requests_for_stream(request_stream: AccountManagerRequestStream) {
        request_stream
            .try_for_each(|r| future::ready(Self::handle_request(r)))
            .unwrap_or_else(|e| warn!("Error running AccountManager {:?}", e))
            .await;
    }

    fn handle_request(request: AccountManagerRequest) -> Result<(), fidl::Error> {
        match request {
            // TODO(zarvox): Enable these methods once the FIDL protocol description includes them
            // TempGetAccount { id, password, account, responder} => {
            // },
            // GetAccountMetadata { id, responder } => {
            // },
            // TempProvisionNewAccount { password, metadata, account, responder } => {
            //   // return id
            // },
            GetAccountIds { responder } => {
                // TODO(zarvox): populate the list based on account partition state instead of
                // hardcoding a reply
                let account_ids = [1u64];
                responder.send(&account_ids)
            }
            GetAccountAuthStates { scenario: _, responder } => {
                let mut resp = Err(Error::UnsupportedOperation);
                responder.send(&mut resp)
            }
            GetAccountMetadata { id: _, responder: _ } => {
                unimplemented!();
            }
            GetAccount { id: _, context_provider: _, account: _, responder } => {
                let mut resp = Err(Error::UnsupportedOperation);
                responder.send(&mut resp)
            }
            DeprecatedGetAccount { id: _, password: _, account: _, responder: _ } => {
                unimplemented!();
            }
            RegisterAccountListener { listener: _, options: _, responder } => {
                let mut resp = Err(Error::UnsupportedOperation);
                responder.send(&mut resp)
            }
            RemoveAccount { id: _, force: _, responder } => {
                let mut resp = Err(Error::UnsupportedOperation);
                responder.send(&mut resp)
            }
            ProvisionNewAccount { lifetime: _, auth_mechanism_id: _, responder } => {
                let mut resp = Err(Error::UnsupportedOperation);
                responder.send(&mut resp)
            }
            DeprecatedProvisionNewAccount {
                password: _,
                metadata: _,
                account: _,
                responder: _,
            } => {
                unimplemented!();
            }

            GetAuthenticationMechanisms { responder } => {
                let mut resp = Err(Error::UnsupportedOperation);
                responder.send(&mut resp)
            }
        }
    }
}
