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
use fuchsia_async as fasync;
use fuchsia_component::client::{launch, App};
use fuchsia_component::fuchsia_single_component_package_url;
use fuchsia_component::server::{NestedEnvironment, ServiceFs};
use futures::future::join;
use futures::prelude::*;
use lazy_static::lazy_static;
use std::ops::Deref;

lazy_static! {
    /// URL for account manager.
    static ref ACCOUNT_MANAGER_URL: String =
        String::from(fuchsia_single_component_package_url!("account_manager"));

    /// Arguments passed to account manager started in test environment.
    static ref ACCOUNT_MANAGER_ARGS: Vec<String> = vec![String::from("--dev-auth-providers")];
}

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

    let (serve_result, provision_result) = await!(join(
        serve_fn,
        account_manager.provision_from_auth_provider(acp_client_end, "dev_auth_provider"),
    ));
    serve_result?;
    match provision_result? {
        (Status::Ok, Some(new_account_id)) => Ok(*new_account_id),
        (status, _) => Err(format_err!("ProvisionFromAuthProvider returned status: {:?}", status)),
    }
}

/// A proxy to an account manager running in a nested environment.
struct NestedAccountManagerProxy {
    /// Proxy to account manager.
    account_manager_proxy: AccountManagerProxy,

    /// Application object for account manager.  Needs to be kept in scope to
    /// keep the nested environment alive.
    _app: App,

    /// The nested environment account manager is running in.  Needs to be kept
    /// in scope to keep the nested environment alive.
    _nested_envronment: NestedEnvironment,
}

impl Deref for NestedAccountManagerProxy {
    type Target = AccountManagerProxy;

    fn deref(&self) -> &AccountManagerProxy {
        &self.account_manager_proxy
    }
}

/// Start account manager in an isolated environment and return a proxy to it. An optional
/// environment label can be provided in order to test state preservation across component
/// termination and restart. If env is None a randomized environment label will be picked
/// to provide isolation across tests.
/// NOTE: Do not reuse environment labels across tests. A NestedAccountManagerProxy should be
/// destroyed before a new one referencing the same enviroment is created.
fn create_account_manager(env: Option<String>) -> Result<NestedAccountManagerProxy, Error> {
    let mut service_fs = ServiceFs::new();

    let nested_environment = match env {
        None => service_fs.create_salted_nested_environment("account_test_env")?,
        Some(label) => service_fs.create_nested_environment(&label)?,
    };

    let app = launch(
        nested_environment.launcher(),
        ACCOUNT_MANAGER_URL.clone(),
        Some(ACCOUNT_MANAGER_ARGS.clone()),
    )?;
    fasync::spawn(service_fs.collect());
    let account_manager_proxy = app.connect_to_service::<AccountManagerMarker>()?;

    Ok(NestedAccountManagerProxy {
        account_manager_proxy,
        _app: app,
        _nested_envronment: nested_environment,
    })
}

// TODO(jsankey): Work with ComponentFramework and cramertj@ to develop a nice Rust equivalent of
// the C++ TestWithEnvironment fixture to provide isolated environments for each test case.  We
// are currently creating a new environment for account manager to run in, but the tests
// themselves run in a single environment.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_provision_new_account() -> Result<(), Error> {
    let account_manager = create_account_manager(None)
        .expect("Failed to launch account manager in nested environment.");

    // Verify we initially have no accounts.
    assert_eq!(await!(account_manager.get_account_ids())?, vec![]);

    // Provision a new account.
    let account_1 = await!(provision_new_account(&account_manager))?;
    assert_eq!(
        await!(account_manager.get_account_ids())?,
        vec![LocalAccountId { id: account_1.id }]
    );

    // Provision a second new account and verify it has a different ID.
    let account_2 = await!(provision_new_account(&account_manager))?;
    assert_ne!(account_1.id, account_2.id);

    let account_ids = await!(account_manager.get_account_ids())?;
    assert_eq!(account_ids.len(), 2);
    assert!(account_ids.contains(&account_1));
    assert!(account_ids.contains(&account_2));

    // Auth state should be unknown for the new accounts
    let (_, auth_states) = await!(account_manager.get_account_auth_states())?;
    assert_eq!(auth_states.len(), 2);
    assert!(auth_states.iter().all(|state| state.auth_state.summary == AuthStateSummary::Unknown
        && account_ids.contains(&state.account_id)));

    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_provision_new_account_from_auth_provider() -> Result<(), Error> {
    let account_manager = create_account_manager(None)
        .expect("Failed to launch account manager in nested environment.");

    // Verify we initially have no accounts.
    assert_eq!(await!(account_manager.get_account_ids())?, vec![]);

    // Provision a new account.
    let account_1 = await!(provision_account_from_dev_auth_provider(&account_manager))?;
    assert_eq!(
        await!(account_manager.get_account_ids())?,
        vec![LocalAccountId { id: account_1.id }]
    );

    // Provision a second new account and verify it has a different ID.
    let account_2 = await!(provision_account_from_dev_auth_provider(&account_manager))?;
    assert_ne!(account_1.id, account_2.id);

    let account_ids = await!(account_manager.get_account_ids())?;
    assert_eq!(account_ids.len(), 2);
    assert!(account_ids.contains(&account_1));
    assert!(account_ids.contains(&account_2));

    // Auth state should be unknown for the new accounts
    let (_, auth_states) = await!(account_manager.get_account_auth_states())?;
    assert_eq!(auth_states.len(), 2);
    assert!(auth_states.iter().all(|state| state.auth_state.summary == AuthStateSummary::Unknown
        && account_ids.contains(&state.account_id)));

    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_get_account_and_persona() -> Result<(), Error> {
    let account_manager = create_account_manager(None)
        .expect("Failed to launch account manager in nested environment.");

    assert_eq!(await!(account_manager.get_account_ids())?, vec![]);

    let mut account = await!(provision_new_account(&account_manager))?;
    // Connect a channel to the newly created account and verify it's usable.
    let (acp_client_end, _) = create_endpoints()?;
    let (account_client_end, account_server_end) = create_endpoints()?;
    assert_eq!(
        await!(account_manager.get_account(&mut account, acp_client_end, account_server_end))?,
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

    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_account_deletion() -> Result<(), Error> {
    let account_manager = create_account_manager(None)
        .expect("Failed to launch account manager in nested environment.");

    assert_eq!(await!(account_manager.get_account_ids())?, vec![]);

    let mut account_1 = await!(provision_new_account(&account_manager))?;
    let account_2 = await!(provision_new_account(&account_manager))?;
    let existing_accounts = await!(account_manager.get_account_ids())?;
    assert!(existing_accounts.contains(&LocalAccountId { id: account_1.id }));
    assert!(existing_accounts.contains(&LocalAccountId { id: account_2.id }));
    assert_eq!(existing_accounts.len(), 2);

    // Delete an account and verify it is removed.
    assert_eq!(await!(account_manager.remove_account(&mut account_1))?, Status::Ok);
    assert_eq!(
        await!(account_manager.get_account_ids())?,
        vec![LocalAccountId { id: account_2.id }]
    );
    // Connecting to the deleted account should fail.
    let (acp_client_end, _acp_server_end) = create_endpoints()?;
    let (_account_client_end, account_server_end) = create_endpoints()?;
    assert_eq!(
        await!(account_manager.get_account(&mut account_1, acp_client_end, account_server_end))?,
        Status::NotFound
    );

    Ok(())
}

/// Ensure that an account manager created in a specific environment picks up the state of
/// previous instances that ran in that environment. Also check that some basic operations work on
/// accounts created in that previous lifetime.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_lifecycle() -> Result<(), Error> {
    let account_manager = create_account_manager(Some("test_account_deletion".to_string()))
        .expect("Failed to launch account manager in nested environment.");

    assert_eq!(await!(account_manager.get_account_ids())?, vec![]);

    let mut account_1 = await!(provision_new_account(&account_manager))?;
    let mut account_2 = await!(provision_new_account(&account_manager))?;
    let existing_accounts = await!(account_manager.get_account_ids())?;
    assert_eq!(existing_accounts.len(), 2);

    // Kill and restart account manager in the same environment
    std::mem::drop(account_manager);
    let account_manager = create_account_manager(Some("test_account_deletion".to_string()))
        .expect("Failed to launch account manager in nested environment.");

    // Retrieve an account that was created in the earlier lifetime
    let (acp_client_end, _) = create_endpoints()?;
    let (_account_client_end, account_server_end) = create_endpoints()?;
    assert_eq!(
        await!(account_manager.get_account(&mut account_1, acp_client_end, account_server_end))?,
        Status::Ok
    );

    // Delete an account and verify it is removed.
    assert_eq!(await!(account_manager.remove_account(&mut account_2))?, Status::Ok);
    assert_eq!(
        await!(account_manager.get_account_ids())?,
        vec![LocalAccountId { id: account_1.id }]
    );

    Ok(())
}
