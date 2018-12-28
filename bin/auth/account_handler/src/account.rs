// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::account_handler::AccountHandler;
use crate::persona::Persona;
use account_common::{FidlLocalPersonaId, LocalAccountId, LocalPersonaId};
use failure::Error;
use fidl::encoding::OutOfLine;
use fidl::endpoints::{ClientEnd, ServerEnd};
use fidl_fuchsia_auth::{AuthChangeGranularity, AuthState, ServiceProviderAccount};
use fidl_fuchsia_auth_account::{
    AccountRequest, AccountRequestStream, AuthListenerMarker, PersonaMarker, Status,
};
use fuchsia_async as fasync;
use futures::prelude::*;
use log::{error, warn};
use std::sync::Arc;

/// Information about the Account that this AccountHandler instance is responsible for.
///
/// This state is only available once the Handler has been initialized to a particular account via
/// the AccountHandlerControl channel.
pub struct Account {
    /// A device-local identifier for this account.
    _id: LocalAccountId,

    /// The default persona for this account.
    default_persona: Arc<Persona>,

    // TODO(jsankey): Once the system and API surface can support more than a single persona, add
    // additional state here to store these personae. This will most likely be a hashmap from
    // LocalPersonaId to Persona struct, and changing default_persona from a struct to an ID.
}

impl Account {
    /// Constructs a new Account.
    pub fn new(account_id: LocalAccountId) -> Account {
        let persona_id = LocalPersonaId::new(rand::random::<u64>());
        Self {
            _id: account_id.clone(),
            default_persona: Arc::new(Persona::new(persona_id, account_id)),
        }
    }

    /// Asynchronously handles the supplied stream of `AccountRequest` messages.
    pub async fn handle_requests_from_stream(
        &self, mut stream: AccountRequestStream,
    ) -> Result<(), Error> {
        while let Some(req) = await!(stream.try_next())? {
            self.handle_request(req)?;
        }
        Ok(())
    }

    /// Dispatches an `AccountRequest` message to the appropriate handler method
    /// based on its type.
    pub fn handle_request(&self, req: AccountRequest) -> Result<(), fidl::Error> {
        match req {
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
                let mut response = self.get_default_persona(persona);
                responder.send(response.0, response.1.as_mut().map(OutOfLine))?;
            }
            AccountRequest::GetPersona {
                id,
                persona,
                responder,
            } => {
                let response = self.get_persona(id.into(), persona);
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

    fn get_auth_state(&self) -> (Status, Option<AuthState>) {
        // TODO(jsankey): Return real authentication state once authenticators exist to create it.
        (Status::Ok, Some(AccountHandler::DEFAULT_AUTH_STATE))
    }

    fn register_auth_listener(
        &self, _listener: ClientEnd<AuthListenerMarker>, _initial_state: bool,
        _granularity: AuthChangeGranularity,
    ) -> Status {
        // TODO(jsankey): Implement this method.
        warn!("RegisterAuthListener not yet implemented");
        Status::InternalError
    }

    fn get_persona_ids(&self) -> Vec<FidlLocalPersonaId> {
        vec![self.default_persona.id().clone().into()]
    }

    fn get_default_persona(
        &self, persona_server_end: ServerEnd<PersonaMarker>,
    ) -> (Status, Option<FidlLocalPersonaId>) {
        let persona_clone = Arc::clone(&self.default_persona);
        match persona_server_end.into_stream() {
            Ok(stream) => {
                fasync::spawn(
                    async move {
                        await!(persona_clone.handle_requests_from_stream(stream))
                            .unwrap_or_else(|e| error!("Error handling Persona channel {:?}", e))
                    },
                );
                (Status::Ok, Some(self.default_persona.id().clone().into()))
            }
            Err(e) => {
                error!("Error opening Persona channel {:?}", e);
                (Status::IoError, None)
            }
        }
    }

    fn get_persona(
        &self, id: LocalPersonaId, persona_server_end: ServerEnd<PersonaMarker>,
    ) -> Status {
        if &id == self.default_persona.id() {
            self.get_default_persona(persona_server_end).0
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
    use fidl::endpoints::RequestStream;
    use fidl_fuchsia_auth_account::{AccountProxy, AccountRequestStream, PersonaProxy};
    use fuchsia_async as fasync;
    use fuchsia_zircon as zx;

    const TEST_ACCOUNT_ID: u64 = 111111;

    fn create_test_object() -> Account {
        Account::new(LocalAccountId::new(TEST_ACCOUNT_ID))
    }

    fn request_stream_test<TestFn, Fut>(test_object: Account, test_fn: TestFn)
    where
        TestFn: FnOnce(AccountProxy) -> Fut,
        Fut: Future<Output = Result<(), Error>>,
    {
        let mut executor = fasync::Executor::new().expect("Failed to create executor");
        let (server_chan, client_chan) = zx::Channel::create().expect("Failed to create channel");
        let proxy = AccountProxy::new(fasync::Channel::from_channel(client_chan).unwrap());
        let request_stream =
            AccountRequestStream::from_channel(fasync::Channel::from_channel(server_chan).unwrap());

        fasync::spawn(
            async move {
                await!(test_object.handle_requests_from_stream(request_stream))
                    .unwrap_or_else(|err| panic!("Fatal error handling test request: {:?}", err))
            },
        );

        executor
            .run_singlethreaded(test_fn(proxy))
            .expect("Executor run failed.")
    }

    #[test]
    fn test_random_persona_id() {
        // Generating two accounts with the same accountID should lead to two different persona IDs
        let account_1 = Account::new(LocalAccountId::new(TEST_ACCOUNT_ID));
        let account_2 = Account::new(LocalAccountId::new(TEST_ACCOUNT_ID));
        assert_ne!(
            account_1.default_persona.id(),
            account_2.default_persona.id()
        );
    }

    #[test]
    fn test_get_auth_state() {
        request_stream_test(create_test_object(), async move |proxy| {
            assert_eq!(
                await!(proxy.get_auth_state())?,
                (
                    Status::Ok,
                    Some(Box::new(AccountHandler::DEFAULT_AUTH_STATE))
                )
            );
            Ok(())
        });
    }

    #[test]
    fn test_register_auth_listener() {
        request_stream_test(create_test_object(), async move |proxy| {
            let (_server_chan, client_chan) = zx::Channel::create().unwrap();
            let listener = ClientEnd::new(client_chan);
            assert_eq!(
                await!(proxy.register_auth_listener(
                    listener,
                    true, /* include initial state */
                    &mut AuthChangeGranularity {
                        summary_changes: true
                    }
                ))?,
                Status::InternalError
            );
            Ok(())
        });
    }

    #[test]
    fn test_get_persona_ids() {
        // Note: Persona ID is random. Record the persona_id before starting the test.
        let account = create_test_object();
        let persona_id = &account.default_persona.id().clone();

        request_stream_test(account, async move |proxy| {
            let response = await!(proxy.get_persona_ids())?;
            assert_eq!(response.len(), 1);
            assert_eq!(&LocalPersonaId::new(response[0].id), persona_id);
            Ok(())
        });
    }

    #[test]
    fn test_get_default_persona() {
        // Note: Persona ID is random. Record the persona_id before starting the test.
        let account = create_test_object();
        let persona_id = &account.default_persona.id().clone();

        request_stream_test(account, async move |account_proxy| {
            let (server_chan, client_chan) = zx::Channel::create().unwrap();
            let response = await!(account_proxy.get_default_persona(ServerEnd::new(server_chan)))?;
            assert_eq!(response.0, Status::Ok);
            assert_eq!(&LocalPersonaId::from(*response.1.unwrap()), persona_id);

            // The persona channel should now be usable.
            let persona_proxy =
                PersonaProxy::new(fasync::Channel::from_channel(client_chan).unwrap());
            assert_eq!(
                await!(persona_proxy.get_auth_state())?,
                (
                    Status::Ok,
                    Some(Box::new(AccountHandler::DEFAULT_AUTH_STATE))
                )
            );

            Ok(())
        });
    }

    #[test]
    fn test_get_persona_by_correct_id() {
        let account = create_test_object();
        let persona_id = account.default_persona.id().clone();

        request_stream_test(account, async move |account_proxy| {
            let (server_chan, client_chan) = zx::Channel::create().unwrap();
            assert_eq!(
                await!(account_proxy.get_persona(
                    &mut FidlLocalPersonaId::from(persona_id),
                    ServerEnd::new(server_chan)
                ))?,
                Status::Ok
            );

            // The persona channel should now be usable.
            let persona_proxy =
                PersonaProxy::new(fasync::Channel::from_channel(client_chan).unwrap());
            assert_eq!(
                await!(persona_proxy.get_auth_state())?,
                (
                    Status::Ok,
                    Some(Box::new(AccountHandler::DEFAULT_AUTH_STATE))
                )
            );

            Ok(())
        });
    }

    #[test]
    fn test_get_persona_by_incorrect_id() {
        let account = create_test_object();
        // Note: This fixed value has a 1 - 2^64 probability of not matching the randomly chosen
        // one.
        let wrong_id = LocalPersonaId::new(13);

        request_stream_test(account, async move |proxy| {
            let (server_chan, _) = zx::Channel::create().unwrap();
            assert_eq!(
                await!(proxy.get_persona(&mut wrong_id.into(), ServerEnd::new(server_chan)))?,
                Status::NotFound
            );

            Ok(())
        });
    }

    #[test]
    fn test_set_recovery_account() {
        let mut service_provider_account = ServiceProviderAccount {
            identity_provider_domain: "google.com".to_string(),
            user_profile_id: "test_obfuscated_gaia_id".to_string(),
        };

        request_stream_test(create_test_object(), async move |proxy| {
            assert_eq!(
                await!(proxy.set_recovery_account(&mut service_provider_account))?,
                Status::InternalError
            );
            Ok(())
        });
    }

    #[test]
    fn test_get_recovery_account() {
        let expectation = (Status::InternalError, None);
        request_stream_test(create_test_object(), async move |proxy| {
            assert_eq!(await!(proxy.get_recovery_account())?, expectation);
            Ok(())
        });
    }
}
