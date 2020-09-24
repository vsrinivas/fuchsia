// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use account_common::{AccountManagerError, FidlLocalAccountId, LocalAccountId, ResultExt};
use anyhow::{format_err, Error};
use fidl::endpoints::{create_endpoints, ClientEnd, ServerEnd};
use fidl_fuchsia_auth::{AppConfig, AuthenticationContextProviderMarker, Status as AuthStatus};
use fidl_fuchsia_auth::{AuthProviderConfig, UserProfileInfo};
use fidl_fuchsia_identity_account::{
    AccountAuthState, AccountListenerMarker, AccountListenerOptions, AccountManagerRequest,
    AccountManagerRequestStream, AccountMarker, Error as ApiError, Lifetime, Scenario,
};
use fuchsia_component::fuchsia_single_component_package_url;
use fuchsia_inspect::{Inspector, Property};
use futures::lock::Mutex;
use futures::prelude::*;
use lazy_static::lazy_static;
use log::{info, warn};
use std::path::PathBuf;
use std::sync::Arc;

use crate::account_event_emitter::{AccountEvent, AccountEventEmitter};
use crate::account_handler_connection::AccountHandlerConnection;
use crate::account_handler_context::AccountHandlerContext;
use crate::account_map::AccountMap;
use crate::inspect;

const SELF_URL: &str = fuchsia_single_component_package_url!("account_manager");

lazy_static! {

    /// The Auth scopes used for authorization during service provider-based account provisioning.
    /// An empty vector means that the auth provider should use its default scopes.
    static ref APP_SCOPES: Vec<String> = Vec::default();
}

/// The core component of the account system for Fuchsia.
///
/// The AccountManager maintains the set of Fuchsia accounts that are provisioned on the device,
/// launches and configures AuthenticationProvider components to perform authentication via
/// service providers, and launches and delegates to AccountHandler component instances to
/// determine the detailed state and authentication for each account.
///
/// `AHC` An AccountHandlerConnection used to spawn new AccountHandler components.
pub struct AccountManager<AHC: AccountHandlerConnection> {
    /// The account map maintains the state of all accounts as well as connections to their account
    /// handlers.
    account_map: Mutex<AccountMap<AHC>>,

    /// Contains the client ends of all AccountListeners which are subscribed to account events.
    event_emitter: AccountEventEmitter,

    /// Helper for outputting auth_provider information via fuchsia_inspect. Must be retained
    /// to avoid dropping the static properties it contains.
    _auth_providers_inspect: inspect::AuthProviders,
}

impl<AHC: AccountHandlerConnection> AccountManager<AHC> {
    /// Constructs a new AccountManager, loading existing set of accounts from `data_dir`, and an
    /// auth provider configuration. The directory must exist at construction.
    pub fn new(
        data_dir: PathBuf,
        auth_provider_config: &[AuthProviderConfig],
        auth_mechanism_ids: &[String],
        inspector: &Inspector,
    ) -> Result<AccountManager<AHC>, Error> {
        let context =
            Arc::new(AccountHandlerContext::new(auth_provider_config, auth_mechanism_ids));
        let account_map = AccountMap::load(data_dir, Arc::clone(&context), inspector.root())?;

        // Initialize the structs used to output state through the inspect system.
        let auth_providers_inspect = inspect::AuthProviders::new(inspector.root());
        let auth_provider_types: Vec<String> =
            auth_provider_config.iter().map(|apc| apc.auth_provider_type.clone()).collect();
        auth_providers_inspect.types.set(&auth_provider_types.join(","));
        let event_emitter = AccountEventEmitter::new(inspector.root());

        Ok(Self {
            account_map: Mutex::new(account_map),
            event_emitter,
            _auth_providers_inspect: auth_providers_inspect,
        })
    }

    /// Asynchronously handles the supplied stream of `AccountManagerRequest` messages.
    pub async fn handle_requests_from_stream(
        &self,
        mut stream: AccountManagerRequestStream,
    ) -> Result<(), Error> {
        while let Some(req) = stream.try_next().await? {
            self.handle_request(req).await?;
        }
        Ok(())
    }

    /// Handles a single request to the AccountManager.
    pub async fn handle_request(&self, req: AccountManagerRequest) -> Result<(), fidl::Error> {
        match req {
            AccountManagerRequest::GetAccountIds { responder } => {
                let response = self.get_account_ids().await;
                responder.send(&response)?;
            }
            AccountManagerRequest::GetAccountAuthStates { scenario, responder } => {
                let mut response = self.get_account_auth_states(scenario).await;
                responder.send(&mut response)?;
            }
            AccountManagerRequest::GetAccount { id, context_provider, account, responder } => {
                let mut response = self.get_account(id.into(), context_provider, account).await;
                responder.send(&mut response)?;
            }
            AccountManagerRequest::RegisterAccountListener { listener, options, responder } => {
                let mut response = self.register_account_listener(listener, options).await;
                responder.send(&mut response)?;
            }
            AccountManagerRequest::RemoveAccount { id, force, responder } => {
                let mut response = self.remove_account(id.into(), force).await;
                responder.send(&mut response)?;
            }
            AccountManagerRequest::ProvisionFromAuthProvider {
                auth_context_provider,
                auth_provider_type,
                lifetime,
                auth_mechanism_id,
                responder,
            } => {
                let mut response = self
                    .provision_from_auth_provider(
                        auth_context_provider,
                        auth_provider_type,
                        lifetime,
                        auth_mechanism_id,
                    )
                    .await;
                responder.send(&mut response)?;
            }
            AccountManagerRequest::ProvisionNewAccount {
                lifetime,
                auth_mechanism_id,
                responder,
            } => {
                let mut response = self.provision_new_account(lifetime, auth_mechanism_id).await;
                responder.send(&mut response)?;
            }
            AccountManagerRequest::GetAuthenticationMechanisms { responder } => {
                responder.send(&mut Err(ApiError::UnsupportedOperation))?;
            }
        }
        Ok(())
    }

    async fn get_account_ids(&self) -> Vec<FidlLocalAccountId> {
        self.account_map.lock().await.get_account_ids().iter().map(|id| id.clone().into()).collect()
    }

    async fn get_account_auth_states(
        &self,
        _scenario: Scenario,
    ) -> Result<Vec<AccountAuthState>, ApiError> {
        // TODO(jsankey): Collect authentication state from AccountHandler instances rather than
        // returning a fixed value. This will involve opening account handler connections (in
        // parallel) for all of the accounts where encryption keys for the account's data partition
        // are available.
        return Err(ApiError::UnsupportedOperation);
    }

    async fn get_account(
        &self,
        id: LocalAccountId,
        auth_context_provider: ClientEnd<AuthenticationContextProviderMarker>,
        account: ServerEnd<AccountMarker>,
    ) -> Result<(), ApiError> {
        let mut account_map = self.account_map.lock().await;
        let account_handler = account_map.get_handler(&id).await.map_err(|err| {
            warn!("Failure getting account handler connection: {:?}", err);
            err.api_error
        })?;
        account_handler.proxy().unlock_account().await.map_err(|_| ApiError::Resource)??;

        account_handler.proxy().get_account(auth_context_provider, account).await.map_err(
            |err| {
                warn!("Failure calling get account: {:?}", err);
                ApiError::Resource
            },
        )?
    }

    async fn register_account_listener(
        &self,
        listener: ClientEnd<AccountListenerMarker>,
        options: AccountListenerOptions,
    ) -> Result<(), ApiError> {
        match (&options.scenario, &options.granularity) {
            (None, Some(_)) => return Err(ApiError::InvalidRequest),
            (Some(_), _) => return Err(ApiError::UnsupportedOperation),
            (None, None) => {}
        };
        let account_ids = self.account_map.lock().await.get_account_ids();
        let proxy = listener.into_proxy().map_err(|err| {
            warn!("Could not convert AccountListener client end to proxy {:?}", err);
            ApiError::InvalidRequest
        })?;
        self.event_emitter.add_listener(proxy, options, &account_ids).await.map_err(|err| {
            warn!("Could not instantiate AccountListener client {:?}", err);
            ApiError::Unknown
        })?;
        info!("AccountListener established");
        Ok(())
    }

    async fn remove_account(
        &self,
        account_id: LocalAccountId,
        force: bool,
    ) -> Result<(), ApiError> {
        let mut account_map = self.account_map.lock().await;
        let account_handler = account_map.get_handler(&account_id).await.map_err(|err| {
            warn!("Could not get account handler for account removal {:?}", err);
            err.api_error
        })?;
        // TODO(43491): Make a conscious decision on what should happen when removing
        // a locked account.
        account_handler.proxy().unlock_account().await.map_err(|_| ApiError::Resource)??;
        account_handler.proxy().remove_account(force).await.map_err(|_| ApiError::Resource)??;
        account_handler.terminate().await;
        // Emphemeral accounts were never included in the StoredAccountList and so it does not need
        // to be modified when they are removed.
        // TODO(fxbug.dev/39455): Handle irrecoverable, corrupt state.
        account_map.remove_account(&account_id).await.map_err(|err| {
            warn!("Could not remove account: {:?}", err);
            // TODO(fxbug.dev/39829): Improve error mapping.
            if err.api_error == ApiError::NotFound {
                // We already checked for existence, so NotFound is unexpected
                ApiError::Internal
            } else {
                err.api_error
            }
        })?;
        let event = AccountEvent::AccountRemoved(account_id.clone());
        self.event_emitter.publish(&event).await;
        Ok(())
    }

    /// Creates a new account handler connection, then creates an account within it,
    /// and finally returns the connection.
    async fn create_account_internal(
        &self,
        lifetime: Lifetime,
        auth_mechanism_id: Option<String>,
    ) -> Result<Arc<AHC>, ApiError> {
        let account_handler =
            self.account_map.lock().await.new_handler(lifetime).map_err(|err| {
                warn!("Could not initialize account handler: {:?}", err);
                err.api_error
            })?;
        account_handler
            .proxy()
            .create_account(auth_mechanism_id.as_ref().map(|x| &**x))
            .await
            .map_err(|err| {
            warn!("Could not create account: {:?}", err);
            ApiError::Resource
        })??;
        Ok(account_handler)
    }

    async fn provision_new_account(
        &self,
        lifetime: Lifetime,
        auth_mechanism_id: Option<String>,
    ) -> Result<FidlLocalAccountId, ApiError> {
        let account_handler = self.create_account_internal(lifetime, auth_mechanism_id).await?;
        let account_id = account_handler.get_account_id();

        // Persist the account both in memory and on disk
        if let Err(err) = self.add_account(account_handler.clone()).await {
            warn!("Failure adding account: {:?}", err);
            account_handler.terminate().await;
            Err(err.api_error)
        } else {
            info!("Adding new local account {:?}", account_id);
            Ok(account_id.clone().into())
        }
    }

    async fn provision_from_auth_provider(
        &self,
        auth_context_provider: ClientEnd<AuthenticationContextProviderMarker>,
        auth_provider_type: String,
        lifetime: Lifetime,
        auth_mechanism_id: Option<String>,
    ) -> Result<FidlLocalAccountId, ApiError> {
        let account_handler = self.create_account_internal(lifetime, auth_mechanism_id).await?;
        let account_id = account_handler.get_account_id();

        // Add a service provider to the account
        let _user_profile = match Self::add_service_provider_account(
            auth_context_provider,
            auth_provider_type,
            Arc::clone(&account_handler),
        )
        .await
        {
            Ok(user_profile) => user_profile,
            Err(err) => {
                // TODO(dnordstrom): Remove the newly created account handler as a cleanup.
                warn!("Failure adding service provider account: {:?}", err);
                account_handler.terminate().await;
                return Err(err.api_error);
            }
        };

        // Persist the account both in memory and on disk
        if let Err(err) = self.add_account(Arc::clone(&account_handler)).await {
            warn!("Failure adding service provider account: {:?}", err);
            account_handler.terminate().await;
            Err(err.api_error)
        } else {
            info!("Adding new account {:?}", &account_id);
            Ok(account_id.clone().into())
        }
    }

    // Attach a service provider account to this Fuchsia account
    async fn add_service_provider_account(
        auth_context_provider: ClientEnd<AuthenticationContextProviderMarker>,
        auth_provider_type: String,
        account_handler: Arc<AHC>,
    ) -> Result<UserProfileInfo, AccountManagerError> {
        // Use account handler to get a channel to the account
        let (account_client_end, account_server_end) =
            create_endpoints().account_manager_error(ApiError::Resource)?;
        account_handler.proxy().get_account(auth_context_provider, account_server_end).await??;
        let account_proxy =
            account_client_end.into_proxy().account_manager_error(ApiError::Resource)?;

        // Use the account to get the persona
        let (persona_client_end, persona_server_end) =
            create_endpoints().account_manager_error(ApiError::Resource)?;
        account_proxy.get_default_persona(persona_server_end).await??;
        let persona_proxy =
            persona_client_end.into_proxy().account_manager_error(ApiError::Resource)?;

        // Use the persona to get the token manager
        let (tm_client_end, tm_server_end) =
            create_endpoints().account_manager_error(ApiError::Resource)?;
        persona_proxy.get_token_manager(SELF_URL, tm_server_end).await??;
        let tm_proxy = tm_client_end.into_proxy().account_manager_error(ApiError::Resource)?;

        // Use the token manager to authorize
        let mut app_config = AppConfig {
            auth_provider_type,
            client_id: None,
            client_secret: None,
            redirect_uri: None,
        };
        let fut = tm_proxy.authorize(
            &mut app_config,
            None, /* auth_ui_context */
            &mut APP_SCOPES.iter().map(|x| &**x),
            None, /* user_profile_id */
            None, /* auth_code */
        );
        match fut.await? {
            (AuthStatus::Ok, None) => Err(AccountManagerError::new(ApiError::Internal)
                .with_cause(format_err!("Invalid response from token manager"))),
            (AuthStatus::Ok, Some(user_profile)) => Ok(*user_profile),
            (auth_status, _) => Err(AccountManagerError::from(auth_status)),
        }
    }

    // Add the account to the AccountManager, including persistent state.
    async fn add_account(&self, account_handler: Arc<AHC>) -> Result<(), AccountManagerError> {
        let mut account_map = self.account_map.lock().await;

        account_map.add_account(Arc::clone(&account_handler)).await.map_err(|err| {
            warn!("Could not add account: {:?}", err);
            // TODO(fxbug.dev/39829): Improve error mapping.
            if err.api_error == ApiError::FailedPrecondition {
                ApiError::Internal
            } else {
                err.api_error
            }
        })?;
        let event = AccountEvent::AccountAdded(account_handler.get_account_id().clone());
        self.event_emitter.publish(&event).await;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::account_handler_connection::AccountHandlerConnectionImpl;
    use crate::stored_account_list::{StoredAccountList, StoredAccountMetadata};
    use fidl::endpoints::{create_request_stream, RequestStream};
    use fidl_fuchsia_identity_account::{
        AccountListenerRequest, AccountManagerProxy, AccountManagerRequestStream,
        AuthChangeGranularity, InitialAccountState, Scenario, ThreatScenario,
    };
    use fuchsia_async as fasync;
    use fuchsia_inspect::{assert_inspect_tree, Inspector};
    use fuchsia_zircon as zx;
    use futures::future::join;
    use lazy_static::lazy_static;
    use std::path::Path;
    use tempfile::TempDir;

    type TestAccountManager = AccountManager<AccountHandlerConnectionImpl>;

    lazy_static! {
        /// Configuration for a set of fake auth providers used for testing.
        /// This can be populated later if needed.
        static ref AUTH_PROVIDER_CONFIG: Vec<AuthProviderConfig> = vec![];

        static ref AUTH_MECHANISM_IDS: Vec<String> = vec![];

        static ref TEST_SCENARIO: Scenario =
            Scenario { include_test: false, threat_scenario: ThreatScenario::BasicAttacker };

        static ref TEST_GRANULARITY: AuthChangeGranularity = AuthChangeGranularity {
            engagement_changes: true,
            presence_changes: false,
            summary_changes: true,
        };
    }

    const FORCE_REMOVE_ON: bool = true;

    fn request_stream_test<TestFn, Fut>(account_manager: TestAccountManager, test_fn: TestFn)
    where
        TestFn: FnOnce(AccountManagerProxy, Arc<TestAccountManager>) -> Fut,
        Fut: Future<Output = Result<(), Error>>,
    {
        let mut executor = fasync::Executor::new().expect("Failed to create executor");
        let (server_chan, client_chan) = zx::Channel::create().expect("Failed to create channel");
        let proxy = AccountManagerProxy::new(fasync::Channel::from_channel(client_chan).unwrap());
        let request_stream = AccountManagerRequestStream::from_channel(
            fasync::Channel::from_channel(server_chan).unwrap(),
        );

        let account_manager_arc = Arc::new(account_manager);
        let account_manager_clone = Arc::clone(&account_manager_arc);
        // TODO(fxbug.dev/39745): Migrate off of fuchsia_async::spawn.
        fasync::Task::spawn(async move {
            account_manager_clone
                .handle_requests_from_stream(request_stream)
                .await
                .unwrap_or_else(|err| panic!("Fatal error handling test request: {:?}", err))
        })
        .detach();

        executor
            .run_singlethreaded(test_fn(proxy, account_manager_arc))
            .expect("Executor run failed.")
    }

    // Construct an account manager initialized with the supplied set of accounts.
    fn create_accounts(
        existing_ids: Vec<u64>,
        data_dir: &Path,
        inspector: &Inspector,
    ) -> TestAccountManager {
        let stored_account_list = existing_ids
            .iter()
            .map(|&id| StoredAccountMetadata::new(LocalAccountId::new(id)))
            .collect();
        StoredAccountList::new(stored_account_list)
            .save(data_dir)
            .expect("Couldn't write account list");

        read_accounts(data_dir, inspector)
    }

    // Contructs an account manager that reads its accounts from the supplied directory.
    fn read_accounts(
        data_dir: &Path,
        inspector: &Inspector,
    ) -> AccountManager<AccountHandlerConnectionImpl> {
        AccountManager::new(
            data_dir.to_path_buf(),
            &AUTH_PROVIDER_CONFIG,
            &AUTH_MECHANISM_IDS,
            inspector,
        )
        .unwrap()
    }

    /// Note: Many AccountManager methods launch instances of an AccountHandler. Since its
    /// currently not convenient to mock out this component launching in Rust, we rely on the
    /// hermetic component test to provide coverage for these areas and only cover the in-process
    /// behavior with this unit-test.

    #[test]
    fn test_new() {
        let inspector = Inspector::new();
        let data_dir = TempDir::new().unwrap();
        request_stream_test(
            AccountManager::new(
                data_dir.path().into(),
                &AUTH_PROVIDER_CONFIG,
                &AUTH_MECHANISM_IDS,
                &inspector,
            )
            .unwrap(),
            |proxy, _| async move {
                assert_eq!(proxy.get_account_ids().await?.len(), 0);
                Ok(())
            },
        );
    }

    #[test]
    fn test_unsupported_scenario() {
        let data_dir = TempDir::new().unwrap();
        request_stream_test(
            create_accounts(vec![], data_dir.path(), &Inspector::new()),
            |proxy, _test_object| async move {
                assert_eq!(proxy.get_account_ids().await?.len(), 0);
                assert_eq!(
                    proxy.get_account_auth_states(&mut TEST_SCENARIO.clone()).await?,
                    Err(ApiError::UnsupportedOperation)
                );
                Ok(())
            },
        );
    }

    #[test]
    fn test_initially_empty() {
        let data_dir = TempDir::new().unwrap();
        let inspector = Inspector::new();
        request_stream_test(
            create_accounts(vec![], data_dir.path(), &inspector),
            |proxy, _test_object| async move {
                assert_eq!(proxy.get_account_ids().await?.len(), 0);
                assert_inspect_tree!(inspector, root: contains {
                    accounts: {
                        active: 0 as u64,
                        total: 0 as u64,
                    },
                    listeners: {
                        active: 0 as u64,
                        events: 0 as u64,
                        total_opened: 0 as u64,
                    },
                });
                Ok(())
            },
        );
    }

    #[test]
    fn test_remove_missing_account() {
        // Manually create an account manager with one account.
        let data_dir = TempDir::new().unwrap();
        let stored_account_list =
            StoredAccountList::new(vec![StoredAccountMetadata::new(LocalAccountId::new(1))]);
        stored_account_list.save(data_dir.path()).unwrap();
        let inspector = Inspector::new();
        request_stream_test(read_accounts(data_dir.path(), &inspector), |proxy, _test_object| {
            async move {
                // Try to delete a very different account from the one we added.
                assert_eq!(
                    proxy.remove_account(LocalAccountId::new(42).into(), FORCE_REMOVE_ON).await?,
                    Err(ApiError::NotFound)
                );
                assert_inspect_tree!(inspector, root: contains {
                    accounts: {
                        total: 1 as u64,
                        active: 0 as u64,
                    },
                });
                Ok(())
            }
        });
    }

    /// Sets up an AccountListener which an init event.
    #[test]
    fn test_account_listener() {
        let mut options = AccountListenerOptions {
            initial_state: true,
            add_account: true,
            remove_account: true,
            scenario: None,
            granularity: None,
        };

        let data_dir = TempDir::new().unwrap();
        let inspector = Inspector::new();
        request_stream_test(
            create_accounts(vec![1, 2], data_dir.path(), &inspector),
            |proxy, _| {
                async move {
                    let (client_end, mut stream) =
                        create_request_stream::<AccountListenerMarker>().unwrap();
                    let serve_fut = async move {
                        let request = stream.try_next().await.expect("stream error");
                        if let Some(AccountListenerRequest::OnInitialize {
                            account_states,
                            responder,
                        }) = request
                        {
                            assert_eq!(
                                account_states,
                                vec![
                                    InitialAccountState { account_id: 1, auth_state: None },
                                    InitialAccountState { account_id: 2, auth_state: None },
                                ]
                            );
                            responder.send().unwrap();
                        } else {
                            panic!("Unexpected message received");
                        };
                        if let Some(_) = stream.try_next().await.expect("stream error") {
                            panic!("Unexpected message, channel should be closed");
                        }
                    };
                    let request_fut = async move {
                        // The registering itself triggers the init event.
                        assert_eq!(
                            proxy
                                .register_account_listener(client_end, &mut options)
                                .await
                                .unwrap(),
                            Ok(())
                        );
                    };
                    join(request_fut, serve_fut).await;
                    Ok(())
                }
            },
        );
    }

    /// Registers an account listener with invalid request arguments.
    #[test]
    fn test_account_listener_invalid_requests() {
        let mut options = AccountListenerOptions {
            initial_state: true,
            add_account: true,
            remove_account: true,
            scenario: None,
            granularity: Some(Box::new(TEST_GRANULARITY.clone())),
        };

        let data_dir = TempDir::new().unwrap();
        let inspector = Inspector::new();
        request_stream_test(
            create_accounts(vec![1, 2], data_dir.path(), &inspector),
            |proxy, _| async move {
                let (client_end, _) = create_request_stream::<AccountListenerMarker>().unwrap();
                assert_eq!(
                    proxy.register_account_listener(client_end, &mut options).await?,
                    Err(ApiError::InvalidRequest)
                );
                Ok(())
            },
        );
    }

    /// Registers an account listener with a scenario, which is currently unsupported.
    #[test]
    fn test_account_listener_scenario() {
        let mut options = AccountListenerOptions {
            initial_state: true,
            add_account: true,
            remove_account: true,
            scenario: Some(Box::new(TEST_SCENARIO.clone())),
            granularity: None,
        };

        let data_dir = TempDir::new().unwrap();
        let inspector = Inspector::new();
        request_stream_test(
            create_accounts(vec![1, 2], data_dir.path(), &inspector),
            |proxy, _| async move {
                let (client_end, _) = create_request_stream::<AccountListenerMarker>().unwrap();
                assert_eq!(
                    proxy.register_account_listener(client_end, &mut options).await?,
                    Err(ApiError::UnsupportedOperation)
                );
                Ok(())
            },
        );
    }

    /// Registers an account listener with a scenario and a granularity, which is currently
    /// unsupported.
    #[test]
    fn test_account_listener_scenario_granularity() {
        let mut options = AccountListenerOptions {
            initial_state: true,
            add_account: true,
            remove_account: true,
            scenario: Some(Box::new(TEST_SCENARIO.clone())),
            granularity: Some(Box::new(TEST_GRANULARITY.clone())),
        };

        let data_dir = TempDir::new().unwrap();
        let inspector = Inspector::new();
        request_stream_test(
            create_accounts(vec![1, 2], data_dir.path(), &inspector),
            |proxy, _| async move {
                let (client_end, _) = create_request_stream::<AccountListenerMarker>().unwrap();
                assert_eq!(
                    proxy.register_account_listener(client_end, &mut options).await?,
                    Err(ApiError::UnsupportedOperation)
                );
                Ok(())
            },
        );
    }
}
