// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::account_handler::AccountHandler;
use crate::TokenManager;
use account_common::{LocalAccountId, LocalPersonaId};
use failure::Error;
use fidl::encoding::OutOfLine;
use fidl::endpoints::{ClientEnd, ServerEnd};
use fidl_fuchsia_auth::{
    AuthChangeGranularity, AuthState, AuthenticationContextProviderProxy, TokenManagerMarker,
};
use fidl_fuchsia_auth_account::{AuthListenerMarker, PersonaRequest, PersonaRequestStream, Status};
use fuchsia_async as fasync;
use futures::prelude::*;
use log::warn;
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
pub struct Persona {
    /// A device-local identifier for this persona.
    id: LocalPersonaId,

    /// The device-local identitier that this persona is a facet of.
    _account_id: LocalAccountId,

    /// The token manager to be used for authentication token requests.
    token_manager: Arc<TokenManager>,
}

impl Persona {
    /// Constructs a new Persona.
    pub fn new(
        id: LocalPersonaId,
        account_id: LocalAccountId,
        token_manager: Arc<TokenManager>,
    ) -> Persona {
        Self { id, _account_id: account_id, token_manager }
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
    ) -> Result<(), Error> {
        while let Some(req) = await!(stream.try_next())? {
            self.handle_request(context, req)?;
        }
        Ok(())
    }

    /// Dispatches a `PersonaRequest` message to the appropriate handler method
    /// based on its type.
    pub fn handle_request<'a>(
        &self,
        context: &'a PersonaContext,
        req: PersonaRequest,
    ) -> Result<(), fidl::Error> {
        match req {
            PersonaRequest::GetAuthState { responder } => {
                let mut response = self.get_auth_state();
                responder.send(response.0, response.1.as_mut().map(OutOfLine))?;
            }
            PersonaRequest::RegisterAuthListener {
                listener,
                initial_state,
                granularity,
                responder,
            } => {
                let response = self.register_auth_listener(listener, initial_state, granularity);
                responder.send(response)?;
            }
            PersonaRequest::GetTokenManager { application_url, token_manager, responder } => {
                let response = self.get_token_manager(context, application_url, token_manager);
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
        &self,
        _listener: ClientEnd<AuthListenerMarker>,
        _initial_state: bool,
        _granularity: AuthChangeGranularity,
    ) -> Status {
        // TODO(jsankey): Implement this method.
        warn!("RegisterAuthListener not yet implemented");
        Status::InternalError
    }

    fn get_token_manager<'a>(
        &'a self,
        context: &'a PersonaContext,
        application_url: String,
        token_manager_server_end: ServerEnd<TokenManagerMarker>,
    ) -> Status {
        let token_manager_clone = Arc::clone(&self.token_manager);
        let token_manager_context = TokenManagerContext {
            application_url,
            auth_ui_context_provider: context.auth_ui_context_provider.clone(),
        };
        match token_manager_server_end.into_stream() {
            Ok(stream) => {
                fasync::spawn(async move {
                    await!(token_manager_clone
                        .handle_requests_from_stream(&token_manager_context, stream))
                    .unwrap_or_else(|err| warn!("Error handling TokenManager channel {:?}", err))
                });
                Status::Ok
            }
            Err(err) => {
                warn!("Error opening TokenManager channel {:?}", err);
                Status::IoError
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::auth_provider_supplier::AuthProviderSupplier;
    use crate::test_util::*;
    use fidl::endpoints::create_endpoints;
    use fidl_fuchsia_auth::AuthenticationContextProviderMarker;
    use fidl_fuchsia_auth_account::{PersonaMarker, PersonaProxy};
    use fidl_fuchsia_auth_account_internal::AccountHandlerContextMarker;
    use fuchsia_async as fasync;

    /// Type to hold the common state require during construction of test objects and execution
    /// of a test, including an async executor and a temporary location in the filesystem.
    struct Test {
        executor: fasync::Executor,
        _location: TempLocation,
        token_manager: Arc<TokenManager>,
    }

    impl Test {
        fn new() -> Test {
            let executor = fasync::Executor::new().expect("Failed to create executor");
            let location = TempLocation::new();
            let (account_handler_context_client_end, _) =
                create_endpoints::<AccountHandlerContextMarker>().unwrap();
            let token_manager = Arc::new(
                TokenManager::new(
                    &location.test_path(),
                    AuthProviderSupplier::new(
                        account_handler_context_client_end.into_proxy().unwrap(),
                    ),
                )
                .unwrap(),
            );
            Test { executor, _location: location, token_manager }
        }

        fn create_persona(&self) -> Persona {
            Persona::new(
                TEST_PERSONA_ID.clone(),
                TEST_ACCOUNT_ID.clone(),
                Arc::clone(&self.token_manager),
            )
        }

        fn run<TestFn, Fut>(&mut self, test_object: Persona, test_fn: TestFn)
        where
            TestFn: FnOnce(PersonaProxy) -> Fut,
            Fut: Future<Output = Result<(), Error>>,
        {
            let (persona_client_end, persona_server_end) =
                create_endpoints::<PersonaMarker>().unwrap();
            let persona_proxy = persona_client_end.into_proxy().unwrap();
            let request_stream = persona_server_end.into_stream().unwrap();

            let (ui_context_provider_client_end, _) =
                create_endpoints::<AuthenticationContextProviderMarker>().unwrap();
            let context = PersonaContext {
                auth_ui_context_provider: ui_context_provider_client_end.into_proxy().unwrap(),
            };

            fasync::spawn(async move {
                await!(test_object.handle_requests_from_stream(&context, request_stream))
                    .unwrap_or_else(|err| panic!("Fatal error handling test request: {:?}", err))
            });

            self.executor.run_singlethreaded(test_fn(persona_proxy)).expect("Executor run failed.")
        }
    }

    #[test]
    fn test_id() {
        let test = Test::new();
        assert_eq!(test.create_persona().id(), &*TEST_PERSONA_ID);
    }

    #[test]
    fn test_get_auth_state() {
        let mut test = Test::new();
        test.run(test.create_persona(), async move |proxy| {
            assert_eq!(
                await!(proxy.get_auth_state())?,
                (Status::Ok, Some(Box::new(AccountHandler::DEFAULT_AUTH_STATE)))
            );
            Ok(())
        });
    }

    #[test]
    fn test_register_auth_listener() {
        let mut test = Test::new();
        test.run(test.create_persona(), async move |proxy| {
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
        });
    }

    #[test]
    fn test_get_token_manager() {
        let mut test = Test::new();
        test.run(test.create_persona(), async move |proxy| {
            let (token_manager_client_end, token_manager_server_end) = create_endpoints().unwrap();
            assert_eq!(
                await!(proxy.get_token_manager(&TEST_APPLICATION_URL, token_manager_server_end))?,
                Status::Ok
            );

            // The token manager channel should now be usable.
            let token_manager_proxy = token_manager_client_end.into_proxy().unwrap();
            let mut app_config = create_dummy_app_config();
            assert_eq!(
                await!(token_manager_proxy.list_profile_ids(&mut app_config))?,
                (fidl_fuchsia_auth::Status::Ok, vec![])
            );

            Ok(())
        });
    }
}
