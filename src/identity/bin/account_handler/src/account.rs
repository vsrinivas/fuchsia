// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::auth_provider_supplier::AuthProviderSupplier;
use crate::common::AccountLifetime;
use crate::inspect;
use crate::persona::{Persona, PersonaContext};
use crate::stored_account::StoredAccount;
use crate::TokenManager;
use account_common::{
    AccountManagerError, FidlLocalPersonaId, GlobalAccountId, LocalPersonaId, ResultExt,
};
use failure::Error;
use fidl::endpoints::{ClientEnd, ServerEnd};
use fidl_fuchsia_auth::{AuthenticationContextProviderProxy, ServiceProviderAccount};
use fidl_fuchsia_identity_account::{
    AccountRequest, AccountRequestStream, AuthChangeGranularity, AuthListenerMarker, AuthState,
    Error as ApiError, Lifetime, PersonaMarker, Scenario, MAX_ID_SIZE,
};
use fidl_fuchsia_identity_internal::AccountHandlerContextProxy;
use fuchsia_inspect::{Node, NumericProperty};
use futures::prelude::*;
use identity_common::{cancel_or, TaskGroup, TaskGroupCancel};
use identity_key_manager::KeyManager;
use log::{error, info, warn};
use rand::Rng;
use scopeguard;
use std::fs;
use std::sync::Arc;

/// The file name to use for a token manager database. The location is supplied
/// by `AccountHandlerContext.GetAccountPath()`
const TOKEN_DB: &str = "tokens.json";

const GLOBAL_ACCOUNT_ID_SIZE: usize = MAX_ID_SIZE as usize;

/// The context that a particular request to an Account should be executed in, capturing
/// information that was supplied upon creation of the channel.
pub struct AccountContext {
    /// An `AuthenticationContextProviderProxy` capable of generating new `AuthenticationUiContext`
    /// channels.
    pub auth_ui_context_provider: AuthenticationContextProviderProxy,
}

/// Information about the Account that this AccountHandler instance is responsible for.
///
/// This state is only available once the Handler has been initialized to a particular account via
/// the AccountHandlerControl channel.
pub struct Account {
    /// A global identifier for this account.
    global_id: GlobalAccountId,

    /// Lifetime for this account.
    lifetime: Arc<AccountLifetime>,

    /// The default persona for this account.
    default_persona: Arc<Persona>,

    /// Collection of tasks that are using this instance.
    task_group: TaskGroup,

    /// Helper for outputting account information via fuchsia_inspect.
    inspect: inspect::Account,
    // TODO(jsankey): Once the system and API surface can support more than a single persona, add
    // additional state here to store these personae. This will most likely be a hashmap from
    // LocalPersonaId to Persona struct, and changing default_persona from a struct to an ID. We
    // will also need to store Arc<TokenManager> at the account level.
}

impl Account {
    /// A fixed string returned as the name of all accounts until account names are fully
    /// implemented.
    const DEFAULT_ACCOUNT_NAME: &'static str = "Unnamed account";

    /// Manually construct an account object, shouldn't normally be called directly.
    async fn new(
        persona_id: LocalPersonaId,
        global_account_id: GlobalAccountId,
        lifetime: AccountLifetime,
        context_proxy: AccountHandlerContextProxy,
        inspect_parent: &Node,
    ) -> Result<Account, AccountManagerError> {
        let task_group = TaskGroup::new();
        let token_manager_task_group = task_group
            .create_child()
            .await
            .map_err(|_| AccountManagerError::new(ApiError::RemovalInProgress))?;
        let key_manager_task_group = task_group
            .create_child()
            .await
            .map_err(|_| AccountManagerError::new(ApiError::RemovalInProgress))?;
        let default_persona_task_group = task_group
            .create_child()
            .await
            .map_err(|_| AccountManagerError::new(ApiError::RemovalInProgress))?;
        let auth_provider_supplier = AuthProviderSupplier::new(context_proxy);
        let token_manager = Arc::new(match &lifetime {
            AccountLifetime::Ephemeral => {
                TokenManager::new_in_memory(auth_provider_supplier, token_manager_task_group)
            }
            AccountLifetime::Persistent { account_dir } => {
                let token_db_path = account_dir.join(TOKEN_DB);
                TokenManager::new(&token_db_path, auth_provider_supplier, token_manager_task_group)
                    .account_manager_error(ApiError::Unknown)?
            }
        });
        let key_manager = Arc::new(KeyManager::new(key_manager_task_group));
        let lifetime = Arc::new(lifetime);
        let account_inspect = inspect::Account::new(inspect_parent);
        Ok(Self {
            global_id: global_account_id,
            lifetime: Arc::clone(&lifetime),
            default_persona: Arc::new(Persona::new(
                persona_id,
                lifetime,
                token_manager,
                key_manager,
                default_persona_task_group,
                inspect_parent,
            )),
            task_group,
            inspect: account_inspect,
        })
    }

    /// Creates a new Fuchsia account and, if it is persistent, stores it on disk.
    pub async fn create(
        lifetime: AccountLifetime,
        context_proxy: AccountHandlerContextProxy,
        inspect_parent: &Node,
    ) -> Result<Account, AccountManagerError> {
        let global_account_id = Self::generate_global_account_id()?;
        let local_persona_id = LocalPersonaId::new(rand::random::<u64>());
        if let AccountLifetime::Persistent { ref account_dir } = lifetime {
            if StoredAccount::path(account_dir).exists() {
                info!("Attempting to create account twice");
                return Err(AccountManagerError::new(ApiError::Internal));
            }
            let stored_account =
                StoredAccount::new(local_persona_id.clone(), global_account_id.clone());
            stored_account.save(account_dir)?;
        }
        Self::new(local_persona_id, global_account_id, lifetime, context_proxy, inspect_parent)
            .await
    }

    /// Loads an existing Fuchsia account from disk.
    pub async fn load(
        lifetime: AccountLifetime,
        context_proxy: AccountHandlerContextProxy,
        inspect_parent: &Node,
    ) -> Result<Account, AccountManagerError> {
        let account_dir = match lifetime {
            AccountLifetime::Persistent { ref account_dir } => account_dir,
            AccountLifetime::Ephemeral => {
                warn!(concat!(
                    "Attempting to load an ephemeral account from disk. This is not a ",
                    "supported operation."
                ));
                return Err(AccountManagerError::new(ApiError::Internal));
            }
        };
        let stored_account = StoredAccount::load(account_dir)?;
        let local_persona_id = stored_account.get_default_persona_id().clone();
        let global_account_id = stored_account.get_global_account_id().clone();
        Self::new(local_persona_id, global_account_id, lifetime, context_proxy, inspect_parent)
            .await
    }

    /// Removes the account from disk or returns the account and the error.
    pub fn remove(self) -> Result<(), (Self, AccountManagerError)> {
        self.remove_inner().map_err(|err| (self, err))
    }

    /// Removes the account from disk.
    fn remove_inner(&self) -> Result<(), AccountManagerError> {
        match self.lifetime.as_ref() {
            AccountLifetime::Ephemeral => Ok(()),
            AccountLifetime::Persistent { account_dir } => {
                let token_db_path = &account_dir.join(TOKEN_DB);
                if token_db_path.exists() {
                    fs::remove_file(token_db_path).map_err(|err| {
                        warn!("Failed to delete token db: {:?}", err);
                        AccountManagerError::new(ApiError::Resource).with_cause(err)
                    })?;
                }
                let to_remove = StoredAccount::path(&account_dir.clone());
                fs::remove_file(to_remove).map_err(|err| {
                    warn!("Failed to delete account doc: {:?}", err);
                    AccountManagerError::new(ApiError::Resource).with_cause(err)
                })
            }
        }
    }

    /// Returns a task group which can be used to spawn and cancel tasks that use this instance.
    pub fn task_group(&self) -> &TaskGroup {
        &self.task_group
    }

    /// A global identifier for this account
    pub fn global_id(&self) -> &GlobalAccountId {
        &self.global_id
    }

    /// Asynchronously handles the supplied stream of `AccountRequest` messages.
    pub async fn handle_requests_from_stream<'a>(
        &'a self,
        context: &'a AccountContext,
        mut stream: AccountRequestStream,
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

    /// Dispatches an `AccountRequest` message to the appropriate handler method
    /// based on its type.
    pub async fn handle_request<'a>(
        &'a self,
        context: &'a AccountContext,
        req: AccountRequest,
    ) -> Result<(), fidl::Error> {
        match req {
            AccountRequest::GetAccountName { responder } => {
                let response = self.get_account_name();
                responder.send(&response)?;
            }
            AccountRequest::GetLifetime { responder } => {
                let response = self.get_lifetime();
                responder.send(response)?;
            }
            AccountRequest::GetAuthState { scenario, responder } => {
                let mut response = self.get_auth_state(scenario);
                responder.send(&mut response)?;
            }
            AccountRequest::RegisterAuthListener {
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
            AccountRequest::GetPersonaIds { responder } => {
                let mut response = self.get_persona_ids().into_iter();
                responder.send(&mut response)?;
            }
            AccountRequest::GetDefaultPersona { persona, responder } => {
                let mut response = self.get_default_persona(context, persona).await;
                responder.send(&mut response)?;
            }
            AccountRequest::GetPersona { id, persona, responder } => {
                let mut response = self.get_persona(context, id.into(), persona).await;
                responder.send(&mut response)?;
            }
            AccountRequest::GetRecoveryAccount { responder } => {
                let mut response =
                    self.get_recovery_account().map(|option| option.map(|value| Box::new(value)));
                responder.send(&mut response)?;
            }
            AccountRequest::SetRecoveryAccount { account, responder } => {
                let mut response = self.set_recovery_account(account);
                responder.send(&mut response)?;
            }
            AccountRequest::GetAuthMechanismEnrollments { responder, .. } => {
                responder.send(&mut Err(ApiError::UnsupportedOperation))?;
            }
            AccountRequest::CreateAuthMechanismEnrollment { responder, .. } => {
                responder.send(&mut Err(ApiError::UnsupportedOperation))?;
            }
            AccountRequest::RemoveAuthMechanismEnrollment { responder, .. } => {
                responder.send(&mut Err(ApiError::UnsupportedOperation))?;
            }
            AccountRequest::Lock { responder } => {
                responder.send(&mut Err(ApiError::UnsupportedOperation))?;
            }
        }
        Ok(())
    }

    fn generate_global_account_id() -> Result<GlobalAccountId, AccountManagerError> {
        let mut rng = rand::thread_rng();
        let mut bytes = vec![0u8; GLOBAL_ACCOUNT_ID_SIZE];
        rng.try_fill(bytes.as_mut_slice()).account_manager_error(ApiError::Resource)?;
        Ok(GlobalAccountId::new(bytes))
    }

    fn get_lifetime(&self) -> Lifetime {
        Lifetime::from(self.lifetime.as_ref())
    }

    fn get_account_name(&self) -> String {
        // TODO(dnordstrom, jsankey): Implement this method, initially by populating the name from
        // an associated service provider account profile name or a randomly assigned string.
        Self::DEFAULT_ACCOUNT_NAME.to_string()
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

    fn get_persona_ids(&self) -> Vec<FidlLocalPersonaId> {
        vec![self.default_persona.id().clone().into()]
    }

    async fn get_default_persona<'a>(
        &'a self,
        context: &'a AccountContext,
        persona_server_end: ServerEnd<PersonaMarker>,
    ) -> Result<FidlLocalPersonaId, ApiError> {
        let persona_clone = Arc::clone(&self.default_persona);
        let persona_context =
            PersonaContext { auth_ui_context_provider: context.auth_ui_context_provider.clone() };
        let stream = persona_server_end.into_stream().map_err(|err| {
            error!("Error opening Persona channel {:?}", err);
            ApiError::Resource
        })?;
        self.default_persona
            .task_group()
            .spawn(|cancel| async move {
                persona_clone
                    .handle_requests_from_stream(&persona_context, stream, cancel)
                    .await
                    .unwrap_or_else(|e| error!("Error handling Persona channel {:?}", e))
            })
            .await
            .map_err(|_| ApiError::RemovalInProgress)?;
        Ok(self.default_persona.id().clone().into())
    }

    async fn get_persona<'a>(
        &'a self,
        context: &'a AccountContext,
        id: LocalPersonaId,
        persona_server_end: ServerEnd<PersonaMarker>,
    ) -> Result<(), ApiError> {
        if &id == self.default_persona.id() {
            self.get_default_persona(context, persona_server_end).await.map(|_| ())
        } else {
            warn!("Requested persona does not exist {:?}", id);
            Err(ApiError::NotFound)
        }
    }

    fn get_recovery_account(&self) -> Result<Option<ServiceProviderAccount>, ApiError> {
        // TODO(jsankey): Implement this method.
        warn!("GetRecoveryAccount not yet implemented");
        Err(ApiError::Internal)
    }

    fn set_recovery_account(&self, _account: ServiceProviderAccount) -> Result<(), ApiError> {
        // TODO(jsankey): Implement this method.
        warn!("SetRecoveryAccount not yet implemented");
        Err(ApiError::Internal)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_util::*;
    use fidl::endpoints::create_endpoints;
    use fidl_fuchsia_auth::AuthenticationContextProviderMarker;
    use fidl_fuchsia_identity_account::{AccountMarker, AccountProxy, Scenario, ThreatScenario};
    use fidl_fuchsia_identity_internal::AccountHandlerContextMarker;
    use fuchsia_async as fasync;
    use fuchsia_inspect::Inspector;

    const TEST_SCENARIO: Scenario =
        Scenario { include_test: false, threat_scenario: ThreatScenario::BasicAttacker };

    const TEST_AUTH_MECHANISM_ID: &str = "<AUTH MECHANISM ID>";

    const TEST_ENROLLMENT_ID: u64 = 1337;

    /// Type to hold the common state require during construction of test objects and execution
    /// of a test, including an async executor and a temporary location in the filesystem.
    struct Test {
        location: TempLocation,
    }

    impl Test {
        fn new() -> Test {
            Test { location: TempLocation::new() }
        }

        async fn create_persistent_account(&self) -> Result<Account, AccountManagerError> {
            let inspector = Inspector::new();
            let (account_handler_context_client_end, _) =
                create_endpoints::<AccountHandlerContextMarker>().unwrap();
            let account_dir = self.location.path.clone();
            Account::create(
                AccountLifetime::Persistent { account_dir },
                account_handler_context_client_end.into_proxy().unwrap(),
                &inspector.root(),
            )
            .await
        }

        async fn create_ephemeral_account(&self) -> Result<Account, AccountManagerError> {
            let inspector = Inspector::new();
            let (account_handler_context_client_end, _) =
                create_endpoints::<AccountHandlerContextMarker>().unwrap();
            Account::create(
                AccountLifetime::Ephemeral,
                account_handler_context_client_end.into_proxy().unwrap(),
                &inspector.root(),
            )
            .await
        }

        async fn load_account(&self) -> Result<Account, AccountManagerError> {
            let inspector = Inspector::new();
            let (account_handler_context_client_end, _) =
                create_endpoints::<AccountHandlerContextMarker>().unwrap();
            Account::load(
                AccountLifetime::Persistent { account_dir: self.location.path.clone() },
                account_handler_context_client_end.into_proxy().unwrap(),
                &inspector.root(),
            )
            .await
        }

        async fn run<TestFn, Fut>(&mut self, test_object: Account, test_fn: TestFn)
        where
            TestFn: FnOnce(AccountProxy) -> Fut,
            Fut: Future<Output = Result<(), Error>>,
        {
            let (account_client_end, account_server_end) =
                create_endpoints::<AccountMarker>().unwrap();
            let account_proxy = account_client_end.into_proxy().unwrap();
            let request_stream = account_server_end.into_stream().unwrap();

            let (ui_context_provider_client_end, _) =
                create_endpoints::<AuthenticationContextProviderMarker>().unwrap();
            let context = AccountContext {
                auth_ui_context_provider: ui_context_provider_client_end.into_proxy().unwrap(),
            };

            let task_group = TaskGroup::new();

            task_group
                .spawn(|cancel| async move {
                    test_object
                        .handle_requests_from_stream(&context, request_stream, cancel)
                        .await
                        .unwrap_or_else(|err| {
                            panic!("Fatal error handling test request: {:?}", err)
                        })
                })
                .await
                .expect("Unable to spawn task");
            test_fn(account_proxy).await.expect("Test function failed.")
        }
    }

    #[fasync::run_until_stalled(test)]
    async fn test_random_identifiers() {
        let mut test = Test::new();
        // Generating two accounts with the same accountID should lead to two different persona IDs
        let account_1 = test.create_persistent_account().await.unwrap();
        test.location = TempLocation::new();
        let account_2 = test.create_persistent_account().await.unwrap();
        assert_ne!(account_1.default_persona.id(), account_2.default_persona.id());
        assert_ne!(account_1.global_id(), account_2.global_id());
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_account_name() {
        let mut test = Test::new();
        test.run(test.create_persistent_account().await.unwrap(), |proxy| async move {
            assert_eq!(proxy.get_account_name().await?, Account::DEFAULT_ACCOUNT_NAME);
            Ok(())
        })
        .await;
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_lifetime_ephemeral() {
        let mut test = Test::new();
        test.run(test.create_ephemeral_account().await.unwrap(), |proxy| async move {
            assert_eq!(proxy.get_lifetime().await?, Lifetime::Ephemeral);
            Ok(())
        })
        .await;
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_lifetime_persistent() {
        let mut test = Test::new();
        test.run(test.create_persistent_account().await.unwrap(), |proxy| async move {
            assert_eq!(proxy.get_lifetime().await?, Lifetime::Persistent);
            Ok(())
        })
        .await;
    }

    #[fasync::run_until_stalled(test)]
    async fn test_create_and_load() {
        let test = Test::new();
        // Persists the account on disk
        let account_1 = test.create_persistent_account().await.unwrap();
        // Reads from same location
        let account_2 = test.load_account().await.unwrap();

        // Since persona ids are random, we can check that loading worked successfully here
        assert_eq!(account_1.default_persona.id(), account_2.default_persona.id());
    }

    #[fasync::run_until_stalled(test)]
    async fn test_load_non_existing() {
        let test = Test::new();
        assert!(test.load_account().await.is_err()); // Reads from uninitialized location
    }

    /// Attempting to load an ephemeral account fails.
    #[fasync::run_until_stalled(test)]
    async fn test_load_ephemeral() {
        let inspector = Inspector::new();
        let (account_handler_context_client_end, _) =
            create_endpoints::<AccountHandlerContextMarker>().unwrap();
        assert!(Account::load(
            AccountLifetime::Ephemeral,
            account_handler_context_client_end.into_proxy().unwrap(),
            &inspector.root(),
        )
        .await
        .is_err());
    }

    #[fasync::run_until_stalled(test)]
    async fn test_create_twice() {
        let test = Test::new();
        assert!(test.create_persistent_account().await.is_ok());
        assert!(test.create_persistent_account().await.is_err()); // Tries to write to same dir
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_auth_state() {
        let mut test = Test::new();
        test.run(test.create_persistent_account().await.unwrap(), |proxy| async move {
            assert_eq!(
                proxy.get_auth_state(&mut TEST_SCENARIO.clone()).await?,
                Err(ApiError::UnsupportedOperation)
            );
            Ok(())
        })
        .await;
    }

    #[fasync::run_until_stalled(test)]
    async fn test_register_auth_listener() {
        let mut test = Test::new();
        test.run(test.create_persistent_account().await.unwrap(), |proxy| {
            async move {
                let (auth_listener_client_end, _) = create_endpoints().unwrap();
                assert_eq!(
                    proxy
                        .register_auth_listener(
                            &mut TEST_SCENARIO.clone(),
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
    async fn test_get_persona_ids() {
        let mut test = Test::new();
        // Note: Persona ID is random. Record the persona_id before starting the test.
        let account = test.create_persistent_account().await.unwrap();
        let persona_id = &account.default_persona.id().clone();

        test.run(account, |proxy| async move {
            let response = proxy.get_persona_ids().await?;
            assert_eq!(response.len(), 1);
            assert_eq!(&LocalPersonaId::new(response[0]), persona_id);
            Ok(())
        })
        .await;
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_default_persona() {
        let mut test = Test::new();
        // Note: Persona ID is random. Record the persona_id before starting the test.
        let account = test.create_persistent_account().await.unwrap();
        let persona_id = &account.default_persona.id().clone();

        test.run(account, |account_proxy| {
            async move {
                let (persona_client_end, persona_server_end) = create_endpoints().unwrap();
                let response = account_proxy.get_default_persona(persona_server_end).await?;
                assert_eq!(&LocalPersonaId::from(response.unwrap()), persona_id);

                // The persona channel should now be usable.
                let persona_proxy = persona_client_end.into_proxy().unwrap();
                assert_eq!(
                    persona_proxy.get_auth_state(&mut TEST_SCENARIO.clone()).await?,
                    Err(ApiError::UnsupportedOperation)
                );
                assert_eq!(persona_proxy.get_lifetime().await?, Lifetime::Persistent);

                Ok(())
            }
        })
        .await;
    }

    /// When an ephemeral account is created, its default persona is also ephemeral.
    #[fasync::run_until_stalled(test)]
    async fn test_ephemeral_account_has_ephemeral_persona() {
        let mut test = Test::new();
        let account = test.create_ephemeral_account().await.unwrap();
        test.run(account, |account_proxy| async move {
            let (persona_client_end, persona_server_end) = create_endpoints().unwrap();
            assert!(account_proxy.get_default_persona(persona_server_end).await?.is_ok());
            let persona_proxy = persona_client_end.into_proxy().unwrap();

            assert_eq!(persona_proxy.get_lifetime().await?, Lifetime::Ephemeral);
            Ok(())
        })
        .await;
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_persona_by_correct_id() {
        let mut test = Test::new();
        let account = test.create_persistent_account().await.unwrap();
        let persona_id = account.default_persona.id().clone();

        test.run(account, |account_proxy| {
            async move {
                let (persona_client_end, persona_server_end) = create_endpoints().unwrap();
                assert!(account_proxy
                    .get_persona(FidlLocalPersonaId::from(persona_id), persona_server_end)
                    .await?
                    .is_ok());

                // The persona channel should now be usable.
                let persona_proxy = persona_client_end.into_proxy().unwrap();
                assert_eq!(
                    persona_proxy.get_auth_state(&mut TEST_SCENARIO.clone()).await?,
                    Err(ApiError::UnsupportedOperation)
                );

                Ok(())
            }
        })
        .await;
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_persona_by_incorrect_id() {
        let mut test = Test::new();
        let account = test.create_persistent_account().await.unwrap();
        // Note: This fixed value has a 1 - 2^64 probability of not matching the randomly chosen
        // one.
        let wrong_id = LocalPersonaId::new(13);

        test.run(account, |proxy| async move {
            let (_, persona_server_end) = create_endpoints().unwrap();
            assert_eq!(
                proxy.get_persona(wrong_id.into(), persona_server_end).await?,
                Err(ApiError::NotFound)
            );

            Ok(())
        })
        .await;
    }

    #[fasync::run_until_stalled(test)]
    async fn test_set_recovery_account() {
        let mut test = Test::new();
        let mut service_provider_account = ServiceProviderAccount {
            identity_provider_domain: "google.com".to_string(),
            user_profile_id: "test_obfuscated_gaia_id".to_string(),
        };

        test.run(test.create_persistent_account().await.unwrap(), |proxy| async move {
            assert_eq!(
                proxy.set_recovery_account(&mut service_provider_account).await?,
                Err(ApiError::Internal)
            );
            Ok(())
        })
        .await;
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_recovery_account() {
        let mut test = Test::new();
        let expectation = Err(ApiError::Internal);
        test.run(test.create_persistent_account().await.unwrap(), |proxy| async move {
            assert_eq!(proxy.get_recovery_account().await?, expectation);
            Ok(())
        })
        .await;
    }

    #[fasync::run_until_stalled(test)]
    async fn test_auth_mechanisms() {
        let mut test = Test::new();
        test.run(test.create_persistent_account().await.unwrap(), |proxy| async move {
            assert_eq!(
                proxy.get_auth_mechanism_enrollments().await?,
                Err(ApiError::UnsupportedOperation)
            );
            assert_eq!(
                proxy.create_auth_mechanism_enrollment(TEST_AUTH_MECHANISM_ID).await?,
                Err(ApiError::UnsupportedOperation)
            );
            assert_eq!(
                proxy.remove_auth_mechanism_enrollment(TEST_ENROLLMENT_ID).await?,
                Err(ApiError::UnsupportedOperation)
            );
            Ok(())
        })
        .await;
    }

    #[fasync::run_until_stalled(test)]
    async fn test_lock() {
        let mut test = Test::new();
        test.run(test.create_persistent_account().await.unwrap(), |proxy| async move {
            assert_eq!(proxy.lock().await?, Err(ApiError::UnsupportedOperation));
            Ok(())
        })
        .await;
    }
}
