// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{format_err, Error};
use fidl::endpoints::{create_endpoints, create_request_stream};
use fidl_fuchsia_auth::{
    AuthStateSummary, AuthenticationContextProviderMarker, AuthenticationContextProviderRequest,
};
use fidl_fuchsia_auth_account::{
    AccountManagerMarker, AccountManagerProxy, LocalAccountId, Status,
};
use futures::prelude::*;

/// Calls provision_new_account on the supplied account_manager, returning an error on any
/// non-OK responses, or the account ID on success.
async fn provision_new_account(
    account_manager: &AccountManagerProxy,
) -> Result<LocalAccountId, Error> {
    match await!(account_manager.provision_new_account())? {
        (Status::Ok, Some(new_account_id)) => Ok(*new_account_id),
        (status, _) => Err(format_err!("ProvisionNewAccount returned status: {:?}", status)),
    }
}

/// Calls provision_from_auth_provider on the supplied account_manager using "dev_auth_provider",
/// mocks out the UI context, returning an error on any non-OK responses, or the account ID on
/// success. This requires the account manager to run the dev_auth_provider. See the
/// `account_manager` package for more information on how that is configured.
async fn provision_account_from_dev_auth_provider(
    account_manager: &AccountManagerProxy,
) -> Result<LocalAccountId, Error> {
    let (acp_client_end, mut acp_request_stream) =
        create_request_stream::<AuthenticationContextProviderMarker>()
            .expect("failed opening channel");

    // This async function mocks out the auth context provider channel, although it supplies no data
    // for the test.
    let serve_fn = async move {
        let request = await!(acp_request_stream.try_next())
            .expect("AuthenticationContextProvider failed receiving message");
        match request {
            Some(AuthenticationContextProviderRequest::GetAuthenticationUiContext { .. }) => Ok(()),
            None => Err(format_err!("AuthenticationContextProvider channel closed unexpectedly")),
        }
    };

    let (serve_result, provision_result) = await!(serve_fn
        .join(account_manager.provision_from_auth_provider(acp_client_end, "dev_auth_provider")));
    serve_result?;
    match provision_result? {
        (Status::Ok, Some(new_account_id)) => Ok(*new_account_id),
        (status, _) => Err(format_err!("ProvisionFromAuthProvider returned status: {:?}", status)),
    }
}

// TODO(jsankey): Work with ComponentFramework and cramertj@ to develop a nice Rust equivalent of
// the C++ TestWithEnvironment fixture to provide isolated environments for each test case. For now
// we verify all functionality in a single test case.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_account_functionality() -> Result<(), Error> {
    let account_manager = fuchsia_component::client::connect_to_service::<AccountManagerMarker>()
        .expect("Failed to connect to account manager service");

    // Verify we initially have no accounts.
    assert_eq!(await!(account_manager.get_account_ids())?, vec![]);

    // Provision a new account.
    let mut account_1 = await!(provision_new_account(&account_manager))?;
    assert_eq!(
        await!(account_manager.get_account_ids())?,
        vec![LocalAccountId { id: account_1.id }]
    );

    // Provision a second new account and verify it has a different ID.
    let account_2 = await!(provision_account_from_dev_auth_provider(&account_manager))?;
    assert_ne!(account_1.id, account_2.id);

    // Connect a channel to one of these accounts and verify it's usable.
    let (acp_client_end, _) = create_endpoints()?;
    let (account_client_end, account_server_end) = create_endpoints()?;
    assert_eq!(
        await!(account_manager.get_account(&mut account_1, acp_client_end, account_server_end))?,
        Status::Ok
    );
    let account_proxy = account_client_end.into_proxy()?;
    let account_auth_state = match await!(account_proxy.get_auth_state())? {
        (Status::Ok, Some(auth_state)) => *auth_state,
        (status, _) => return Err(format_err!("GetAuthState returned status: {:?}", status)),
    };
    assert_eq!(account_auth_state.summary, AuthStateSummary::Unknown);

    // Connect a channel to the account's default persona and verify it's usable.
    let (persona_client_end, persona_server_end) = create_endpoints()?;
    assert_eq!(await!(account_proxy.get_default_persona(persona_server_end))?.0, Status::Ok);
    let persona_proxy = persona_client_end.into_proxy()?;
    let persona_auth_state = match await!(persona_proxy.get_auth_state())? {
        (Status::Ok, Some(auth_state)) => *auth_state,
        (status, _) => return Err(format_err!("GetAuthState returned status: {:?}", status)),
    };
    assert_eq!(persona_auth_state.summary, AuthStateSummary::Unknown);

    // Delete an account and verify it is removed.
    assert_eq!(await!(account_manager.remove_account(&mut account_1))?, Status::Ok);
    assert_eq!(
        await!(account_manager.get_account_ids())?,
        vec![LocalAccountId { id: account_2.id }]
    );
    // Deliberately leave an account as dirty state which will cause assert errors upon storage
    // isolation violations across invocations of this test. No state should be preserved.
    Ok(())
}
