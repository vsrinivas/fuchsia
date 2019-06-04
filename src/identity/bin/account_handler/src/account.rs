// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
extern crate serde;
extern crate serde_json;

use crate::account_handler::AccountHandler;
use crate::auth_provider_supplier::AuthProviderSupplier;
use crate::persona::{Persona, PersonaContext};
use crate::stored_account::StoredAccount;
use crate::TokenManager;
use account_common::{
    AccountManagerError, FidlLocalPersonaId, LocalAccountId, LocalPersonaId, ResultExt,
};
use failure::Error;
use fidl::encoding::OutOfLine;
use fidl::endpoints::{ClientEnd, ServerEnd};
use fidl_fuchsia_auth::{
    AuthChangeGranularity, AuthState, AuthenticationContextProviderProxy, ServiceProviderAccount,
};
use fidl_fuchsia_auth_account::{
    AccountRequest, AccountRequestStream, AuthListenerMarker, PersonaMarker, Status,
};
use fidl_fuchsia_auth_account_internal::AccountHandlerContextProxy;
use futures::prelude::*;
use identity_common::{cancel_or, TaskGroup, TaskGroupCancel};
use log::{error, info, warn};
use std::fs;
use std::path::PathBuf;
use std::sync::Arc;

/// The file name to use for a token manager database. The location is supplied
/// by `AccountHandlerContext.GetAccountPath()`
const TOKEN_DB: &str = "tokens.json";

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
    /// A device-local identifier for this account.
    id: LocalAccountId,

    /// A directory containing data about the Fuchsia account, managed exclusively by one instance
    /// of this type. It should exist prior to constructing an Account object.
    account_dir: PathBuf,

    /// The default persona for this account.
    default_persona: Arc<Persona>,

    /// Collection of tasks that are using this instance.
    task_group: TaskGroup,

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
        account_id: LocalAccountId,
        persona_id: LocalPersonaId,
        account_dir: PathBuf,
        context_proxy: AccountHandlerContextProxy,
    ) -> Result<Account, AccountManagerError> {
        let task_group = TaskGroup::new();
        let token_manager_task_group = await!(task_group.create_child())
            .map_err(|_| AccountManagerError::new(Status::RemovalInProgress))?;
        let default_persona_task_group = await!(task_group.create_child())
            .map_err(|_| AccountManagerError::new(Status::RemovalInProgress))?;
        let token_db_path = account_dir.join(TOKEN_DB);
        let token_manager = Arc::new(
            TokenManager::new(
                &token_db_path,
                AuthProviderSupplier::new(context_proxy),
                token_manager_task_group,
            )
            .account_manager_status(Status::UnknownError)?,
        );
        Ok(Self {
            id: account_id.clone(),
            account_dir,
            default_persona: Arc::new(Persona::new(
                persona_id,
                account_id,
                token_manager,
                default_persona_task_group,
            )),
            task_group,
        })
    }

    /// Creates a new Fuchsia account and persist it on disk.
    pub async fn create(
        account_id: LocalAccountId,
        account_dir: PathBuf,
        context_proxy: AccountHandlerContextProxy,
    ) -> Result<Account, AccountManagerError> {
        if StoredAccount::path(&account_dir).exists() {
            info!("Attempting to create account twice with local id: {:?}", account_id);
            return Err(AccountManagerError::new(Status::InvalidRequest));
        }

        let local_persona_id = LocalPersonaId::new(rand::random::<u64>());
        let stored_account = StoredAccount::new(local_persona_id.clone());
        match stored_account.save(&account_dir) {
            Ok(_) => await!(Self::new(account_id, local_persona_id, account_dir, context_proxy)),
            Err(err) => {
                warn!("Failed to initialize new Account: {:?}", err);
                Err(err)
            }
        }
    }

    /// Loads an existing Fuchsia account from disk.
    pub async fn load(
        account_id: LocalAccountId,
        account_dir: PathBuf,
        context_proxy: AccountHandlerContextProxy,
    ) -> Result<Account, AccountManagerError> {
        let stored_account = StoredAccount::load(&account_dir)?;
        let local_persona_id = stored_account.get_default_persona_id().clone();
        await!(Self::new(account_id, local_persona_id, account_dir, context_proxy))
    }

    /// Removes the account from disk.
    pub fn remove(&self) -> Result<(), AccountManagerError> {
        let token_db_path = &self.account_dir.join(TOKEN_DB);
        if token_db_path.exists() {
            fs::remove_file(token_db_path).map_err(|err| {
                warn!("Failed to delete token db: {:?}", err);
                AccountManagerError::new(Status::IoError).with_cause(err)
            })?;
        }
        fs::remove_file(StoredAccount::path(&self.account_dir)).map_err(|err| {
            warn!("Failed to delete account doc: {:?}", err);
            AccountManagerError::new(Status::IoError).with_cause(err)
        })
    }

    /// Returns a task group which can be used to spawn and cancel tasks that use this instance.
    pub fn task_group(&self) -> &TaskGroup {
        &self.task_group
    }

    /// A device-local identifier for this account
    pub fn id(&self) -> &LocalAccountId {
        &self.id
    }

    /// Asynchronously handles the supplied stream of `AccountRequest` messages.
    pub async fn handle_requests_from_stream<'a>(
        &'a self,
        context: &'a AccountContext,
        mut stream: AccountRequestStream,
        cancel: TaskGroupCancel,
    ) -> Result<(), Error> {
        while let Some(result) = await!(cancel_or(&cancel, stream.try_next())) {
            if let Some(request) = result? {
                await!(self.handle_request(context, request))?;
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
            AccountRequest::GetAuthState { responder } => {
                let mut response = self.get_auth_state();
                responder.send(response.0, response.1.as_mut().map(OutOfLine))?;
            }
            AccountRequest::RegisterAuthListener {
                listener,
                initial_state,
                granularity,
                responder,
            } => {
                let response = self.register_auth_listener(listener, initial_state, granularity);
                responder.send(response)?;
            }
            AccountRequest::GetPersonaIds { responder } => {
                let mut response = self.get_persona_ids();
                responder.send(&mut response.iter_mut())?;
            }
            AccountRequest::GetDefaultPersona { persona, responder } => {
                let mut response = await!(self.get_default_persona(context, persona));
                responder.send(response.0, response.1.as_mut().map(OutOfLine))?;
            }
            AccountRequest::GetPersona { id, persona, responder } => {
                let response = await!(self.get_persona(context, id.into(), persona));
                responder.send(response)?;
            }
            AccountRequest::GetRecoveryAccount { responder } => {
                let mut response = self.get_recovery_account();
                responder.send(response.0, response.1.as_mut().map(OutOfLine))?;
            }
            AccountRequest::SetRecoveryAccount { account, responder } => {
                let response = self.set_recovery_account(account);
                responder.send(response)?;
            }
        }
        Ok(())
    }

    fn get_account_name(&self) -> String {
        // TODO(dnordstrom, jsankey): Implement this method, initially by populating the name from
        // an associated service provider account profile name or a randomly assigned string.
        Self::DEFAULT_ACCOUNT_NAME.to_string()
    }

    fn get_auth_state(&self) -> (Status, Option<AuthState>) {
        // TODO(jsankey): Return real authentication state once authenticators exist to create it.
        (Status::Ok, Some(AccountHandler::DEFAULT_AUTH_STATE))
    }

    fn register_auth_listener(
        &self,
        _listener: ClientEnd<AuthListenerMarker>,
        _initial_state: bool,
        _granularity: AuthChangeGranularity,
    ) -> Status {
        // TODO(jsankey): Implement this method.
        warn!("RegisterAuthListener not yet implemented");
        Status::InternalError
    }

    fn get_persona_ids(&self) -> Vec<FidlLocalPersonaId> {
        vec![self.default_persona.id().clone().into()]
    }

    async fn get_default_persona<'a>(
        &'a self,
        context: &'a AccountContext,
        persona_server_end: ServerEnd<PersonaMarker>,
    ) -> (Status, Option<FidlLocalPersonaId>) {
        let persona_clone = Arc::clone(&self.default_persona);
        let persona_context =
            PersonaContext { auth_ui_context_provider: context.auth_ui_context_provider.clone() };
        match persona_server_end.into_stream() {
            Ok(stream) => {
                match await!(self.default_persona.task_group().spawn(|cancel| async move {
                    await!(persona_clone.handle_requests_from_stream(
                        &persona_context,
                        stream,
                        cancel
                    ))
                    .unwrap_or_else(|e| error!("Error handling Persona channel {:?}", e))
                })) {
                    Err(_) => (Status::RemovalInProgress, None),
                    Ok(()) => (Status::Ok, Some(self.default_persona.id().clone().into())),
                }
            }
            Err(e) => {
                error!("Error opening Persona channel {:?}", e);
                (Status::IoError, None)
            }
        }
    }

    async fn get_persona<'a>(
        &'a self,
        context: &'a AccountContext,
        id: LocalPersonaId,
        persona_server_end: ServerEnd<PersonaMarker>,
    ) -> Status {
        if &id == self.default_persona.id() {
            await!(self.get_default_persona(context, persona_server_end)).0
        } else {
            warn!("Requested persona does not exist {:?}", id);
            Status::NotFound
        }
    }

    fn get_recovery_account(&self) -> (Status, Option<ServiceProviderAccount>) {
        // TODO(jsankey): Implement this method.
        warn!("GetRecoveryAccount not yet implemented");
        (Status::InternalError, None)
    }

    fn set_recovery_account(&self, _account: ServiceProviderAccount) -> Status {
        // TODO(jsankey): Implement this method.
        warn!("SetRecoveryAccount not yet implemented");
        Status::InternalError
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_util::*;
    use fidl::endpoints::create_endpoints;
    use fidl_fuchsia_auth::AuthenticationContextProviderMarker;
    use fidl_fuchsia_auth_account::{AccountMarker, AccountProxy};
    use fidl_fuchsia_auth_account_internal::AccountHandlerContextMarker;
    use fuchsia_async as fasync;

    /// Type to hold the common state require during construction of test objects and execution
    /// of a test, including an async executor and a temporary location in the filesystem.
    struct Test {
        location: TempLocation,
    }

    impl Test {
        fn new() -> Test {
            Test { location: TempLocation::new() }
        }

        async fn create_account(&self) -> Result<Account, AccountManagerError> {
            let (account_handler_context_client_end, _) =
                create_endpoints::<AccountHandlerContextMarker>().unwrap();
            await!(Account::create(
                TEST_ACCOUNT_ID.clone(),
                self.location.path.clone(),
                account_handler_context_client_end.into_proxy().unwrap(),
            ))
        }

        async fn load_account(&self) -> Result<Account, AccountManagerError> {
            let (account_handler_context_client_end, _) =
                create_endpoints::<AccountHandlerContextMarker>().unwrap();
            await!(Account::load(
                TEST_ACCOUNT_ID.clone(),
                self.location.path.clone(),
                account_handler_context_client_end.into_proxy().unwrap(),
            ))
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

            await!(task_group.spawn(|cancel| async move {
                await!(test_object.handle_requests_from_stream(&context, request_stream, cancel))
                    .unwrap_or_else(|err| panic!("Fatal error handling test request: {:?}", err))
            },))
            .expect("Unable to spawn task");
            await!(test_fn(account_proxy)).expect("Test function failed.")
        }
    }

    #[fasync::run_until_stalled(test)]
    async fn test_random_persona_id() {
        let mut test = Test::new();
        // Generating two accounts with the same accountID should lead to two different persona IDs
        let account_1 = await!(test.create_account()).unwrap();
        test.location = TempLocation::new();
        let account_2 = await!(test.create_account()).unwrap();
        assert_ne!(account_1.default_persona.id(), account_2.default_persona.id());
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_account_name() {
        let mut test = Test::new();
        await!(test.run(await!(test.create_account()).unwrap(), async move |proxy| {
            assert_eq!(await!(proxy.get_account_name())?, Account::DEFAULT_ACCOUNT_NAME);
            Ok(())
        }));
    }

    #[fasync::run_until_stalled(test)]
    async fn test_create_and_load() {
        let test = Test::new();
        let account_1 = await!(test.create_account()).unwrap(); // Persists the account on disk
        let account_2 = await!(test.load_account()).unwrap(); // Reads from same location

        // Since persona ids are random, we can check that loading worked successfully here
        assert_eq!(account_1.default_persona.id(), account_2.default_persona.id());
    }

    #[fasync::run_until_stalled(test)]
    async fn test_load_non_existing() {
        let test = Test::new();
        assert!(await!(test.load_account()).is_err()); // Reads from uninitialized location
    }

    #[fasync::run_until_stalled(test)]
    async fn test_create_twice() {
        let test = Test::new();
        assert!(await!(test.create_account()).is_ok());
        assert!(await!(test.create_account()).is_err()); // Tries to write to same dir
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_auth_state() {
        let mut test = Test::new();
        await!(test.run(await!(test.create_account()).unwrap(), async move |proxy| {
            assert_eq!(
                await!(proxy.get_auth_state())?,
                (Status::Ok, Some(Box::new(AccountHandler::DEFAULT_AUTH_STATE)))
            );
            Ok(())
        }));
    }

    #[fasync::run_until_stalled(test)]
    async fn test_register_auth_listener() {
        let mut test = Test::new();
        await!(test.run(await!(test.create_account()).unwrap(), async move |proxy| {
            let (auth_listener_client_end, _) = create_endpoints().unwrap();
            assert_eq!(
                await!(proxy.register_auth_listener(
                    auth_listener_client_end,
                    true, /* include initial state */
                    &mut AuthChangeGranularity { summary_changes: true }
                ))?,
                Status::InternalError
            );
            Ok(())
        }));
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_persona_ids() {
        let mut test = Test::new();
        // Note: Persona ID is random. Record the persona_id before starting the test.
        let account = await!(test.create_account()).unwrap();
        let persona_id = &account.default_persona.id().clone();

        await!(test.run(account, async move |proxy| {
            let response = await!(proxy.get_persona_ids())?;
            assert_eq!(response.len(), 1);
            assert_eq!(&LocalPersonaId::new(response[0].id), persona_id);
            Ok(())
        }));
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_default_persona() {
        let mut test = Test::new();
        // Note: Persona ID is random. Record the persona_id before starting the test.
        let account = await!(test.create_account()).unwrap();
        let persona_id = &account.default_persona.id().clone();

        await!(test.run(account, async move |account_proxy| {
            let (persona_client_end, persona_server_end) = create_endpoints().unwrap();
            let response = await!(account_proxy.get_default_persona(persona_server_end))?;
            assert_eq!(response.0, Status::Ok);
            assert_eq!(&LocalPersonaId::from(*response.1.unwrap()), persona_id);

            // The persona channel should now be usable.
            let persona_proxy = persona_client_end.into_proxy().unwrap();
            assert_eq!(
                await!(persona_proxy.get_auth_state())?,
                (Status::Ok, Some(Box::new(AccountHandler::DEFAULT_AUTH_STATE)))
            );

            Ok(())
        }));
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_persona_by_correct_id() {
        let mut test = Test::new();
        let account = await!(test.create_account()).unwrap();
        let persona_id = account.default_persona.id().clone();

        await!(test.run(account, async move |account_proxy| {
            let (persona_client_end, persona_server_end) = create_endpoints().unwrap();
            assert_eq!(
                await!(account_proxy
                    .get_persona(&mut FidlLocalPersonaId::from(persona_id), persona_server_end))?,
                Status::Ok
            );

            // The persona channel should now be usable.
            let persona_proxy = persona_client_end.into_proxy().unwrap();
            assert_eq!(
                await!(persona_proxy.get_auth_state())?,
                (Status::Ok, Some(Box::new(AccountHandler::DEFAULT_AUTH_STATE)))
            );

            Ok(())
        }));
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_persona_by_incorrect_id() {
        let mut test = Test::new();
        let account = await!(test.create_account()).unwrap();
        // Note: This fixed value has a 1 - 2^64 probability of not matching the randomly chosen
        // one.
        let wrong_id = LocalPersonaId::new(13);

        await!(test.run(account, async move |proxy| {
            let (_, persona_server_end) = create_endpoints().unwrap();
            assert_eq!(
                await!(proxy.get_persona(&mut wrong_id.into(), persona_server_end))?,
                Status::NotFound
            );

            Ok(())
        }));
    }

    #[fasync::run_until_stalled(test)]
    async fn test_set_recovery_account() {
        let mut test = Test::new();
        let mut service_provider_account = ServiceProviderAccount {
            identity_provider_domain: "google.com".to_string(),
            user_profile_id: "test_obfuscated_gaia_id".to_string(),
        };

        await!(test.run(await!(test.create_account()).unwrap(), async move |proxy| {
            assert_eq!(
                await!(proxy.set_recovery_account(&mut service_provider_account))?,
                Status::InternalError
            );
            Ok(())
        }));
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_recovery_account() {
        let mut test = Test::new();
        let expectation = (Status::InternalError, None);
        await!(test.run(await!(test.create_account()).unwrap(), async move |proxy| {
            assert_eq!(await!(proxy.get_recovery_account())?, expectation);
            Ok(())
        }));
    }
}
