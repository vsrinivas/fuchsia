// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, format_err, Error},
    fidl::endpoints::{create_endpoints, ServerEnd},
    fidl_fuchsia_identity_account::{
        AccountManagerGetAccountRequest, AccountManagerMarker,
        AccountManagerProvisionNewAccountRequest, AccountManagerProxy, AccountMarker,
        AccountMetadata, AccountProxy, Error as ApiError, Lifetime,
    },
    fidl_fuchsia_logger::LogSinkMarker,
    fidl_fuchsia_sys2 as fsys2,
    fidl_fuchsia_tracing_provider::RegistryMarker,
    fuchsia_async::{DurationExt, TimeoutExt},
    fuchsia_component_test::{Capability, ChildOptions, RealmBuilder, RealmInstance, Ref, Route},
    fuchsia_zircon as zx,
    futures::prelude::*,
    std::ops::Deref,
};

/// Type alias for the LocalAccountId FIDL type
type LocalAccountId = u64;

const ALWAYS_SUCCEED_AUTH_MECHANISM_ID: &str = "#meta/dev_authenticator_always_succeed.cm";

const ALWAYS_FAIL_AUTHENTICATION_AUTH_MECHANISM_ID: &str =
    "#meta/dev_authenticator_always_fail_authentication.cm";

const ACCOUNT_MANAGER_URL: &'static str = "#meta/account_manager.cm";

const ACCOUNT_MANAGER_COMPONENT_NAME: &'static str = "account_manager";

/// Maximum time between a lock request and when the account is locked
const LOCK_REQUEST_DURATION: zx::Duration = zx::Duration::from_seconds(5);

/// Convenience function to create an account metadata table
/// with the supplied name.
fn create_account_metadata(name: &str) -> AccountMetadata {
    AccountMetadata { name: Some(name.to_string()), ..AccountMetadata::EMPTY }
}

/// Calls provision_new_account on the supplied account_manager, returning an error on any
/// non-OK responses, or the account ID on success.
async fn provision_new_account(
    account_manager: &AccountManagerProxy,
    lifetime: Lifetime,
    auth_mechanism_id: Option<&str>,
    metadata: AccountMetadata,
) -> Result<LocalAccountId, Error> {
    account_manager
        .provision_new_account(AccountManagerProvisionNewAccountRequest {
            lifetime: Some(lifetime),
            auth_mechanism_id: auth_mechanism_id.map(|id| id.to_string()),
            metadata: Some(metadata),
            ..AccountManagerProvisionNewAccountRequest::EMPTY
        })
        .await?
        .map_err(|error| format_err!("ProvisionNewAccount returned error: {:?}", error))
}

/// Convenience function to calls get_account on the supplied account_manager.
async fn get_account(
    account_manager: &AccountManagerProxy,
    account_id: u64,
    account_server_end: ServerEnd<AccountMarker>,
) -> Result<Result<(), ApiError>, fidl::Error> {
    account_manager
        .get_account(AccountManagerGetAccountRequest {
            id: Some(account_id),
            account: Some(account_server_end),
            ..AccountManagerGetAccountRequest::EMPTY
        })
        .await
}

/// Utility function to stop a component using LifecycleController.
async fn stop_component(realm_ref: &RealmInstance, child_name: &str) {
    let lifecycle = realm_ref
        .root
        .connect_to_protocol_at_exposed_dir::<fsys2::LifecycleControllerMarker>()
        .expect("Failed to connect to LifecycleController");
    lifecycle
        .stop(&format!("./{}", child_name), true)
        .await
        .expect(&format!("Failed to stop child: {}", child_name))
        .expect(&format!("Failed to unwrap stop child result: {}", child_name));
}

/// Utility function to start a component using LifecycleController.
async fn start_component(realm_ref: &RealmInstance, child_name: &str) {
    let lifecycle = realm_ref
        .root
        .connect_to_protocol_at_exposed_dir::<fsys2::LifecycleControllerMarker>()
        .expect("Failed to connect to LifecycleController");
    lifecycle
        .start(&format!("./{}", child_name))
        .await
        .expect(&format!("Failed to start child: {}", child_name))
        .expect(&format!("Failed to unwrap start child result: {}", child_name));
}

/// A proxy to an account manager running in a nested environment.
struct NestedAccountManagerProxy {
    /// Proxy to account manager.
    account_manager_proxy: AccountManagerProxy,

    /// The realm instance which the account manager is running in.
    /// Needs to be kept in scope to keep the realm alive.
    _realm_instance: RealmInstance,
}

impl Deref for NestedAccountManagerProxy {
    type Target = AccountManagerProxy;

    fn deref(&self) -> &AccountManagerProxy {
        &self.account_manager_proxy
    }
}

impl NestedAccountManagerProxy {
    /// Stop and start the account_manager component and re-initialize
    /// the nested account_manager_proxy.
    pub async fn restart(&mut self) -> Result<(), Error> {
        stop_component(&self._realm_instance, ACCOUNT_MANAGER_COMPONENT_NAME).await;
        start_component(&self._realm_instance, ACCOUNT_MANAGER_COMPONENT_NAME).await;

        self.account_manager_proxy = self
            ._realm_instance
            .root
            .connect_to_protocol_at_exposed_dir::<AccountManagerMarker>()?;
        Ok(())
    }
}

/// Start account manager in an isolated environment and return a proxy to it.
async fn create_account_manager() -> Result<NestedAccountManagerProxy, Error> {
    let builder = RealmBuilder::new().await?;
    let account_manager =
        builder.add_child("account_manager", ACCOUNT_MANAGER_URL, ChildOptions::new()).await?;
    let dev_authenticator_always_succeed = builder
        .add_child(
            "dev_authenticator_always_succeed",
            ALWAYS_SUCCEED_AUTH_MECHANISM_ID,
            ChildOptions::new(),
        )
        .await?;
    let dev_authenticator_always_fail_authentication = builder
        .add_child(
            "dev_authenticator_always_fail_authentication",
            ALWAYS_FAIL_AUTHENTICATION_AUTH_MECHANISM_ID,
            ChildOptions::new(),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name(
                    "fuchsia.identity.authentication.AlwaysSucceedStorageUnlockMechanism",
                ))
                .from(&dev_authenticator_always_succeed)
                .to(&account_manager),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name(
                    "fuchsia.identity.authentication.AlwaysFailStorageUnlockMechanism",
                ))
                .from(&dev_authenticator_always_fail_authentication)
                .to(&account_manager),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<LogSinkMarker>())
                .from(Ref::parent())
                .to(&account_manager)
                .to(&dev_authenticator_always_fail_authentication)
                .to(&dev_authenticator_always_succeed),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::storage("data"))
                .from(Ref::parent())
                .to(&account_manager),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<RegistryMarker>())
                .from(Ref::parent())
                .to(&account_manager),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<AccountManagerMarker>())
                .from(&account_manager)
                .to(Ref::parent()),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fsys2::LifecycleControllerMarker>())
                .from(Ref::framework())
                .to(Ref::parent()),
        )
        .await?;

    let instance = builder.build().await?;

    let account_manager_proxy =
        instance.root.connect_to_protocol_at_exposed_dir::<AccountManagerMarker>()?;
    Ok(NestedAccountManagerProxy { account_manager_proxy, _realm_instance: instance })
}

/// Locks an account and waits for the channel to close.
async fn lock_and_check(account: &AccountProxy) -> Result<(), Error> {
    account.lock().await?.map_err(|err| anyhow!("Lock failed: {:?}", err))?;
    account
        .take_event_stream()
        .for_each(|_| async move {}) // Drain
        .map(|_| Ok(())) // Completed drain results in ok
        .on_timeout(LOCK_REQUEST_DURATION.after_now(), || Err(anyhow!("Locking timeout exceeded")))
        .await
}

// TODO(jsankey): Work with ComponentFramework and cramertj@ to develop a nice Rust equivalent of
// the C++ TestWithEnvironment fixture to provide isolated environments for each test case.  We
// are currently creating a new environment for account manager to run in, but the tests
// themselves run in a single environment.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_provision_new_account() -> Result<(), Error> {
    let account_manager = create_account_manager().await?;

    // Verify we initially have no accounts.
    assert_eq!(account_manager.get_account_ids().await?, vec![]);

    // Provision a new account.
    let account_1 = provision_new_account(
        &account_manager,
        Lifetime::Persistent,
        None,
        create_account_metadata("test1"),
    )
    .await?;
    assert_eq!(account_manager.get_account_ids().await?, vec![account_1]);

    // Provision a second new account and verify it has a different ID.
    let account_2 = provision_new_account(
        &account_manager,
        Lifetime::Persistent,
        None,
        create_account_metadata("test2"),
    )
    .await?;
    assert_ne!(account_1, account_2);

    // Provision account with an auth mechanism
    let account_3 = provision_new_account(
        &account_manager,
        Lifetime::Persistent,
        Some(ALWAYS_SUCCEED_AUTH_MECHANISM_ID),
        create_account_metadata("test3"),
    )
    .await?;

    let account_ids = account_manager.get_account_ids().await?;
    assert_eq!(account_ids.len(), 3);
    assert!(account_ids.contains(&account_1));
    assert!(account_ids.contains(&account_2));
    assert!(account_ids.contains(&account_3));

    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_provision_then_lock_then_unlock_account() -> Result<(), Error> {
    let account_manager = create_account_manager().await?;

    // Provision account with an auth mechanism that passes authentication
    let account_id = provision_new_account(
        &account_manager,
        Lifetime::Persistent,
        Some(ALWAYS_SUCCEED_AUTH_MECHANISM_ID),
        create_account_metadata("test"),
    )
    .await?;
    let (account_client_end, account_server_end) = create_endpoints()?;
    assert_eq!(get_account(&account_manager, account_id, account_server_end).await?, Ok(()));
    let account_proxy = account_client_end.into_proxy()?;

    // Lock the account and ensure that it's locked
    lock_and_check(&account_proxy).await?;

    // Unlock the account and re-acquire a channel
    let (account_client_end, account_server_end) = create_endpoints()?;
    assert_eq!(get_account(&account_manager, account_id, account_server_end).await?, Ok(()));
    let account_proxy = account_client_end.into_proxy()?;
    assert_eq!(account_proxy.get_lifetime().await.unwrap(), Lifetime::Persistent);
    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_unlock_account() -> Result<(), Error> {
    let mut account_manager = create_account_manager().await?;

    // Provision account with an auth mechanism that passes authentication
    let account_id = provision_new_account(
        &account_manager,
        Lifetime::Persistent,
        Some(ALWAYS_SUCCEED_AUTH_MECHANISM_ID),
        create_account_metadata("test"),
    )
    .await?;

    // Restart the account manager, now the account should be locked
    account_manager.restart().await?;

    // Unlock the account and acquire a channel to it
    let (account_client_end, account_server_end) = create_endpoints()?;
    assert_eq!(get_account(&account_manager, account_id, account_server_end).await?, Ok(()));
    let account_proxy = account_client_end.into_proxy()?;
    assert_eq!(account_proxy.get_lifetime().await.unwrap(), Lifetime::Persistent);
    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_provision_then_lock_then_unlock_fail_authentication() -> Result<(), Error> {
    let account_manager = create_account_manager().await?;

    // Provision account with an auth mechanism that fails authentication
    let account_id = provision_new_account(
        &account_manager,
        Lifetime::Persistent,
        Some(ALWAYS_FAIL_AUTHENTICATION_AUTH_MECHANISM_ID),
        create_account_metadata("test"),
    )
    .await?;
    let (account_client_end, account_server_end) = create_endpoints()?;
    assert_eq!(get_account(&account_manager, account_id, account_server_end).await?, Ok(()));
    let account_proxy = account_client_end.into_proxy()?;

    // Lock the account and ensure that it's locked
    lock_and_check(&account_proxy).await?;

    // Attempting to unlock the account fails with an authentication error
    let (_, account_server_end) = create_endpoints()?;
    assert_eq!(
        get_account(&account_manager, account_id, account_server_end).await?,
        Err(ApiError::FailedAuthentication)
    );
    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_unlock_account_fail_authentication() -> Result<(), Error> {
    let mut account_manager = create_account_manager().await?;

    // Provision account with an auth mechanism that fails authentication
    let account_id = provision_new_account(
        &account_manager,
        Lifetime::Persistent,
        Some(ALWAYS_FAIL_AUTHENTICATION_AUTH_MECHANISM_ID),
        create_account_metadata("test"),
    )
    .await?;

    // Restart the account manager, now the account should be locked
    account_manager.restart().await?;

    // Attempting to unlock the account fails with an authentication error
    let (_, account_server_end) = create_endpoints()?;
    assert_eq!(
        get_account(&account_manager, account_id, account_server_end).await?,
        Err(ApiError::FailedAuthentication)
    );
    Ok(())
}

// This represents two nearly identical tests, one with ephemeral and one with persistent accounts
async fn get_account_and_persona_helper(lifetime: Lifetime) -> Result<(), Error> {
    let account_manager = create_account_manager().await?;

    assert_eq!(account_manager.get_account_ids().await?, vec![]);

    let account_id =
        provision_new_account(&account_manager, lifetime, None, create_account_metadata("test"))
            .await?;
    // Connect a channel to the newly created account and verify it's usable.
    let (account_client_end, account_server_end) = create_endpoints()?;
    assert_eq!(get_account(&account_manager, account_id, account_server_end).await?, Ok(()));
    let account_proxy = account_client_end.into_proxy()?;
    let account_auth_state = account_proxy.get_auth_state().await?;
    assert_eq!(account_auth_state, Err(ApiError::UnsupportedOperation));

    // Cannot lock account not protected by an auth mechanism
    assert_eq!(account_proxy.lock().await?, Err(ApiError::FailedPrecondition));
    assert_eq!(account_proxy.get_lifetime().await?, lifetime);

    // Connect a channel to the account's default persona and verify it's usable.
    let (persona_client_end, persona_server_end) = create_endpoints()?;
    assert!(account_proxy.get_default_persona(persona_server_end).await?.is_ok());
    let persona_proxy = persona_client_end.into_proxy()?;
    let persona_auth_state = persona_proxy.get_auth_state().await?;
    assert_eq!(persona_auth_state, Err(ApiError::UnsupportedOperation));
    assert_eq!(persona_proxy.get_lifetime().await?, lifetime);

    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_get_persistent_account_and_persona() -> Result<(), Error> {
    get_account_and_persona_helper(Lifetime::Persistent).await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_get_ephemeral_account_and_persona() -> Result<(), Error> {
    get_account_and_persona_helper(Lifetime::Ephemeral).await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_account_deletion() -> Result<(), Error> {
    let account_manager = create_account_manager().await?;

    assert_eq!(account_manager.get_account_ids().await?, vec![]);

    let account_1 = provision_new_account(
        &account_manager,
        Lifetime::Persistent,
        None,
        create_account_metadata("test1"),
    )
    .await?;
    let account_2 = provision_new_account(
        &account_manager,
        Lifetime::Persistent,
        None,
        create_account_metadata("test2"),
    )
    .await?;
    let existing_accounts = account_manager.get_account_ids().await?;
    assert!(existing_accounts.contains(&account_1));
    assert!(existing_accounts.contains(&account_2));
    assert_eq!(existing_accounts.len(), 2);

    // Delete an account and verify it is removed.
    assert_eq!(account_manager.remove_account(account_1).await?, Ok(()));
    assert_eq!(account_manager.get_account_ids().await?, vec![account_2]);
    // Connecting to the deleted account should fail.
    let (_account_client_end, account_server_end) = create_endpoints()?;
    assert_eq!(
        get_account(&account_manager, account_1, account_server_end).await?,
        Err(ApiError::NotFound)
    );

    Ok(())
}

/// Ensure that an account manager created in a specific environment picks up the state of
/// previous instances that ran in that environment. Also check that some basic operations work on
/// accounts created in that previous lifetime.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_lifecycle() -> Result<(), Error> {
    let mut account_manager = create_account_manager().await?;

    assert_eq!(account_manager.get_account_ids().await?, vec![]);

    let account_1 = provision_new_account(
        &account_manager,
        Lifetime::Persistent,
        None,
        create_account_metadata("test1"),
    )
    .await?;
    let account_2 = provision_new_account(
        &account_manager,
        Lifetime::Persistent,
        None,
        create_account_metadata("test2"),
    )
    .await?;
    let account_3 = provision_new_account(
        &account_manager,
        Lifetime::Ephemeral,
        None,
        create_account_metadata("test3"),
    )
    .await?;

    let existing_accounts = account_manager.get_account_ids().await?;
    assert_eq!(existing_accounts.len(), 3);

    // Restart account manager
    account_manager.restart().await?;

    let existing_accounts = account_manager.get_account_ids().await?;
    assert_eq!(existing_accounts.len(), 2); // The ephemeral account was dropped

    // Make sure we can't retrieve the ephemeral account
    let (_account_client_end, account_server_end) = create_endpoints()?;
    assert_eq!(
        get_account(&account_manager, account_3, account_server_end).await?,
        Err(ApiError::NotFound)
    );
    // Retrieve a persistent account that was created in the earlier lifetime
    let (_account_client_end, account_server_end) = create_endpoints()?;
    assert_eq!(get_account(&account_manager, account_1, account_server_end).await?, Ok(()));

    // Delete an account and verify it is removed.
    assert_eq!(account_manager.remove_account(account_2).await?, Ok(()));
    assert_eq!(account_manager.get_account_ids().await?, vec![account_1]);

    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_account_metadata_persistence() -> Result<(), Error> {
    let mut account_manager = create_account_manager().await?;
    let account_metadata = create_account_metadata("test1");
    let account_1 = provision_new_account(
        &account_manager,
        Lifetime::Persistent,
        None,
        account_metadata.clone(),
    )
    .await?;

    // Restart account manager
    account_manager.restart().await?;

    assert_eq!(account_manager.get_account_metadata(account_1).await?, Ok(account_metadata));

    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_account_metadata_failures() -> Result<(), Error> {
    let account_manager = create_account_manager().await?;

    // Fail if there is no metadata
    assert_eq!(
        account_manager
        .provision_new_account(AccountManagerProvisionNewAccountRequest {
            lifetime: Some(Lifetime::Persistent),
            auth_mechanism_id: None,
            metadata: None,
            ..AccountManagerProvisionNewAccountRequest::EMPTY
        })
        .await?,
        Err(ApiError::InvalidRequest)
    );

    // Fail if metadata is invalid
    assert_eq!(
        account_manager
        .provision_new_account(AccountManagerProvisionNewAccountRequest {
            lifetime: Some(Lifetime::Persistent),
            auth_mechanism_id: None,
            metadata: Some(AccountMetadata::EMPTY),
            ..AccountManagerProvisionNewAccountRequest::EMPTY
        })
        .await?,
        Err(ApiError::InvalidRequest)
    );

    Ok(())
}