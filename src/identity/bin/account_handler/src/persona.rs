// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common::AccountLifetime;
use crate::inspect;
use crate::TokenManager;
use account_common::LocalPersonaId;
use failure::Error;
use fidl::endpoints::{ClientEnd, ServerEnd};
use fidl_fuchsia_auth::{AuthenticationContextProviderProxy, TokenManagerMarker};
use fidl_fuchsia_identity_account::{
    AuthChangeGranularity, AuthListenerMarker, AuthState, Error as ApiError, Lifetime,
    PersonaRequest, PersonaRequestStream, Scenario,
};
use fidl_fuchsia_identity_keys::KeyManagerMarker;
use fuchsia_inspect::{Node, NumericProperty};
use futures::prelude::*;
use identity_common::{cancel_or, TaskGroup, TaskGroupCancel};
use identity_key_manager::{KeyManager, KeyManagerContext};
use log::{error, warn};
use std::sync::Arc;
use token_manager::TokenManagerContext;

/// The context that a particular request to a Persona should be executed in, capturing
/// information that was supplied upon creation of the channel.
pub struct PersonaContext {
    /// An `AuthenticationContextProviderProxy` capable of generating new `AuthenticationUiContext`
    /// channels.
    pub auth_ui_context_provider: AuthenticationContextProviderProxy,
}

/// Information about one of the Personae withing the Account that this AccountHandler instance is
/// responsible for.
///
/// This state is only available once the Handler has been initialized to a particular account via
/// the AccountHandlerControl channel.
// TODO(dnordstrom): Factor out items that are accessed by both account and persona into its own
// type so they don't need to be individually copied or Arc-wrapped. Here, `token_manager`,
// `lifetime` and `account_id` are candidates.
pub struct Persona {
    /// A device-local identifier for this persona.
    id: LocalPersonaId,

    /// Lifetime for this persona's account (ephemeral or persistent with a path).
    lifetime: Arc<AccountLifetime>,

    /// The token manager to be used for authentication token requests.
    token_manager: Arc<TokenManager>,

    /// The key manager to be used for synchronized key requests.
    key_manager: Arc<KeyManager>,

    /// Collection of tasks that are using this instance.
    task_group: TaskGroup,

    /// Helper for outputting persona information via fuchsia_inspect.
    inspect: inspect::Persona,
}

impl Persona {
    /// Returns a task group which can be used to spawn and cancel tasks that use this instance.
    pub fn task_group(&self) -> &TaskGroup {
        &self.task_group
    }

    /// Constructs a new Persona.
    pub fn new(
        id: LocalPersonaId,
        lifetime: Arc<AccountLifetime>,
        token_manager: Arc<TokenManager>,
        key_manager: Arc<KeyManager>,
        task_group: TaskGroup,
        inspect_parent: &Node,
    ) -> Persona {
        let persona_inspect = inspect::Persona::new(inspect_parent, &id);
        Self { id, lifetime, token_manager, key_manager, task_group, inspect: persona_inspect }
    }

    /// Returns the device-local identifier for this persona.
    pub fn id(&self) -> &LocalPersonaId {
        &self.id
    }

    /// Asynchronously handles the supplied stream of `PersonaRequest` messages.
    pub async fn handle_requests_from_stream<'a>(
        &'a self,
        context: &'a PersonaContext,
        mut stream: PersonaRequestStream,
        cancel: TaskGroupCancel,
    ) -> Result<(), Error> {
        self.inspect.open_client_channels.add(1);
        scopeguard::defer!(self.inspect.open_client_channels.subtract(1));
        while let Some(result) = cancel_or(&cancel, stream.try_next()).await {
            if let Some(request) = result? {
                self.handle_request(context, request).await?;
            } else {
                break;
            }
        }
        Ok(())
    }

    /// Dispatches a `PersonaRequest` message to the appropriate handler method
    /// based on its type.
    pub async fn handle_request<'a>(
        &'a self,
        context: &'a PersonaContext,
        req: PersonaRequest,
    ) -> Result<(), fidl::Error> {
        match req {
            PersonaRequest::GetLifetime { responder } => {
                let response = self.get_lifetime();
                responder.send(response)?;
            }
            PersonaRequest::GetAuthState { scenario, responder } => {
                let mut response = self.get_auth_state(scenario);
                responder.send(&mut response)?;
            }
            PersonaRequest::RegisterAuthListener {
                scenario,
                listener,
                initial_state,
                granularity,
                responder,
            } => {
                let mut response =
                    self.register_auth_listener(scenario, listener, initial_state, granularity);
                responder.send(&mut response)?;
            }
            PersonaRequest::GetTokenManager { application_url, token_manager, responder } => {
                let mut response =
                    self.get_token_manager(context, application_url, token_manager).await;
                responder.send(&mut response)?;
            }
            PersonaRequest::GetKeyManager { application_url, key_manager, responder } => {
                let mut response = self.get_key_manager(application_url, key_manager).await;
                responder.send(&mut response)?;
            }
        }
        Ok(())
    }

    fn get_lifetime(&self) -> Lifetime {
        Lifetime::from(self.lifetime.as_ref())
    }

    fn get_auth_state(&self, _scenario: Scenario) -> Result<AuthState, ApiError> {
        // TODO(jsankey): Return real authentication state once authenticators exist to create it.
        Err(ApiError::UnsupportedOperation)
    }

    fn register_auth_listener(
        &self,
        _scenario: Scenario,
        _listener: ClientEnd<AuthListenerMarker>,
        _initial_state: bool,
        _granularity: AuthChangeGranularity,
    ) -> Result<(), ApiError> {
        // TODO(jsankey): Implement this method.
        warn!("RegisterAuthListener not yet implemented");
        Err(ApiError::UnsupportedOperation)
    }

    async fn get_token_manager<'a>(
        &'a self,
        context: &'a PersonaContext,
        application_url: String,
        token_manager_server_end: ServerEnd<TokenManagerMarker>,
    ) -> Result<(), ApiError> {
        let token_manager_clone = Arc::clone(&self.token_manager);
        let token_manager_context = TokenManagerContext {
            application_url,
            auth_ui_context_provider: context.auth_ui_context_provider.clone(),
        };
        let stream = token_manager_server_end.into_stream().map_err(|err| {
            error!("Error opening TokenManager channel {:?}", err);
            ApiError::Resource
        })?;
        self.token_manager
            .task_group()
            .spawn(|cancel| {
                async move {
                    token_manager_clone
                        .handle_requests_from_stream(&token_manager_context, stream, cancel)
                        .await
                        .unwrap_or_else(|e| error!("Error handling TokenManager channel {:?}", e))
                }
            })
            .await
            .map_err(|_| ApiError::RemovalInProgress)?;
        Ok(())
    }

    async fn get_key_manager<'a>(
        &'a self,
        application_url: String,
        key_manager_server_end: ServerEnd<KeyManagerMarker>,
    ) -> Result<(), ApiError> {
        let key_manager_clone = Arc::clone(&self.key_manager);
        let key_manager_context = KeyManagerContext::new(application_url);
        let stream = key_manager_server_end.into_stream().map_err(|err| {
            error!("Error opening KeyManager channel {:?}", err);
            ApiError::Resource
        })?;
        self.key_manager
            .task_group()
            .spawn(|cancel| {
                async move {
                    key_manager_clone
                        .handle_requests_from_stream(&key_manager_context, stream, cancel)
                        .await
                        .unwrap_or_else(|e| error!("Error handling KeyManager channel {:?}", e))
                }
            })
            .await
            .map_err(|_| ApiError::RemovalInProgress)?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::auth_provider_supplier::AuthProviderSupplier;
    use crate::test_util::*;
    use fidl::endpoints::{create_endpoints, create_proxy, create_proxy_and_stream};
    use fidl_fuchsia_auth::AuthenticationContextProviderMarker;
    use fidl_fuchsia_identity_account::{PersonaMarker, PersonaProxy, ThreatScenario};
    use fidl_fuchsia_identity_internal::AccountHandlerContextMarker;
    use fuchsia_async as fasync;
    use fuchsia_inspect::Inspector;
    use std::path::PathBuf;

    const DEFAULT_SCENARIO: Scenario =
        Scenario { include_test: false, threat_scenario: ThreatScenario::None };

    /// Type to hold the common state require during construction of test objects and execution
    /// of a test, including an async executor and a temporary location in the filesystem.
    struct Test {
        _location: TempLocation,
        token_manager: Arc<TokenManager>,
        key_manager: Arc<KeyManager>,
    }

    impl Test {
        fn new() -> Test {
            let location = TempLocation::new();
            let (account_handler_context_proxy, _) =
                create_proxy::<AccountHandlerContextMarker>().unwrap();
            let token_manager = Arc::new(
                TokenManager::new(
                    &location.test_path(),
                    AuthProviderSupplier::new(account_handler_context_proxy),
                    TaskGroup::new(),
                )
                .unwrap(),
            );
            let key_manager = Arc::new(KeyManager::new(TaskGroup::new()));
            Test { _location: location, token_manager, key_manager }
        }

        fn create_persona(&self) -> Persona {
            let inspector = Inspector::new();
            Persona::new(
                TEST_PERSONA_ID.clone(),
                Arc::new(AccountLifetime::Persistent { account_dir: PathBuf::from("/nowhere") }),
                Arc::clone(&self.token_manager),
                Arc::clone(&self.key_manager),
                TaskGroup::new(),
                inspector.root(),
            )
        }

        fn create_ephemeral_persona(&self) -> Persona {
            let inspector = Inspector::new();
            Persona::new(
                TEST_PERSONA_ID.clone(),
                Arc::new(AccountLifetime::Ephemeral),
                Arc::clone(&self.token_manager),
                Arc::clone(&self.key_manager),
                TaskGroup::new(),
                inspector.root(),
            )
        }

        async fn run<TestFn, Fut>(&mut self, test_object: Persona, test_fn: TestFn)
        where
            TestFn: FnOnce(PersonaProxy) -> Fut,
            Fut: Future<Output = Result<(), Error>>,
        {
            let (persona_proxy, request_stream) =
                create_proxy_and_stream::<PersonaMarker>().unwrap();

            let (auth_ui_context_provider, _) =
                create_proxy::<AuthenticationContextProviderMarker>().unwrap();
            let persona_context = PersonaContext { auth_ui_context_provider };

            let task_group = TaskGroup::new();
            task_group
                .spawn(|cancel| {
                    async move {
                        test_object
                            .handle_requests_from_stream(&persona_context, request_stream, cancel)
                            .await
                            .unwrap_or_else(|err| {
                                panic!("Fatal error handling test request: {:?}", err)
                            });
                    }
                })
                .await
                .expect("Unable to spawn task");
            test_fn(persona_proxy).await.expect("Failed running test fn.")
        }
    }

    #[fasync::run_until_stalled(test)]
    async fn test_id() {
        let test = Test::new();
        assert_eq!(test.create_persona().id(), &*TEST_PERSONA_ID);
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_lifetime_persistent() {
        let test = Test::new();
        assert_eq!(test.create_persona().get_lifetime(), Lifetime::Persistent);
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_lifetime_ephemeral() {
        let test = Test::new();
        assert_eq!(test.create_ephemeral_persona().get_lifetime(), Lifetime::Ephemeral);
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_auth_state() {
        let mut test = Test::new();
        test.run(test.create_persona(), |proxy| {
            async move {
                assert_eq!(
                    proxy.get_auth_state(&mut DEFAULT_SCENARIO.clone()).await?,
                    Err(ApiError::UnsupportedOperation)
                );
                Ok(())
            }
        })
        .await;
    }

    #[fasync::run_until_stalled(test)]
    async fn test_register_auth_listener() {
        let mut test = Test::new();
        test.run(test.create_persona(), |proxy| {
            async move {
                let (auth_listener_client_end, _) = create_endpoints().unwrap();
                assert_eq!(
                    proxy
                        .register_auth_listener(
                            &mut DEFAULT_SCENARIO.clone(),
                            auth_listener_client_end,
                            true, /* include initial state */
                            &mut AuthChangeGranularity {
                                presence_changes: false,
                                engagement_changes: false,
                                summary_changes: true,
                            }
                        )
                        .await?,
                    Err(ApiError::UnsupportedOperation)
                );
                Ok(())
            }
        })
        .await;
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_token_manager() {
        let mut test = Test::new();
        test.run(test.create_persona(), |proxy| {
            async move {
                let (token_manager_proxy, token_manager_server_end) = create_proxy().unwrap();
                assert_eq!(
                    proxy
                        .get_token_manager(&TEST_APPLICATION_URL, token_manager_server_end)
                        .await?,
                    Ok(())
                );

                // The token manager channel should now be usable.
                let mut app_config = create_dummy_app_config();
                assert_eq!(
                    token_manager_proxy.list_profile_ids(&mut app_config).await?,
                    (fidl_fuchsia_auth::Status::Ok, vec![])
                );

                Ok(())
            }
        })
        .await;
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_key_manager() {
        let mut test = Test::new();
        test.run(test.create_persona(), |proxy| {
            async move {
                let (key_manager_proxy, key_manager_server_end) = create_proxy().unwrap();
                assert!(proxy
                    .get_key_manager(&TEST_APPLICATION_URL, key_manager_server_end)
                    .await?
                    .is_ok());

                // Channel should be usable, but key manager isn't implemented yet.
                assert_eq!(
                    key_manager_proxy.delete_key_set("key-set").await?,
                    Err(fidl_fuchsia_identity_keys::Error::UnsupportedOperation)
                );
                Ok(())
            }
        })
        .await;
    }
}
