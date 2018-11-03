// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use account_common::{LocalAccountId, LocalPersonaId};
use crate::account_handler::AccountHandler;
use failure::Error;
use fidl::encoding::OutOfLine;
use fidl::endpoints::{ClientEnd, ServerEnd};
use fidl_fuchsia_auth::{AuthChangeGranularity, AuthState, TokenManagerMarker};
use fidl_fuchsia_auth_account::{AuthListenerMarker, PersonaRequest, PersonaRequestStream, Status};
use futures::prelude::*;
use log::warn;

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
}

impl Persona {
    /// Constructs a new Persona.
    pub fn new(id: LocalPersonaId, account_id: LocalAccountId) -> Persona {
        Self {
            id,
            _account_id: account_id,
        }
    }

    /// Returns the device-local identifier for this persona.
    pub fn id(&self) -> &LocalPersonaId {
        &self.id
    }

    /// Asynchronously handles the supplied stream of `PersonaRequest` messages.
    pub async fn handle_requests_from_stream(
        &self, mut stream: PersonaRequestStream,
    ) -> Result<(), Error> {
        while let Some(req) = await!(stream.try_next())? {
            self.handle_request(req)?;
        }
        Ok(())
    }

    /// Dispatches a `PersonaRequest` message to the appropriate handler method
    /// based on its type.
    pub fn handle_request(&self, req: PersonaRequest) -> Result<(), fidl::Error> {
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
            PersonaRequest::GetTokenManager {
                application_url,
                token_manager,
                responder,
            } => {
                let response = self.get_token_manager(application_url, token_manager);
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

    fn get_token_manager(
        &self, _application_url: String, _token_manager: ServerEnd<TokenManagerMarker>,
    ) -> Status {
        // TODO(jsankey): Implement this method.
        warn!("GetTokenManager not yet implemented");
        Status::InternalError
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::RequestStream;
    use fidl_fuchsia_auth_account::{PersonaProxy, PersonaRequestStream};
    use fuchsia_async as fasync;
    use fuchsia_zircon as zx;

    const TEST_ACCOUNT_ID: u64 = 111111;
    const TEST_PERSONA_ID: u64 = 222222;
    const TEST_APPLICATION_URL: &str = "test_app_url";

    fn create_test_object() -> Persona {
        Persona {
            id: LocalPersonaId::new(TEST_PERSONA_ID),
            _account_id: LocalAccountId::new(TEST_ACCOUNT_ID),
        }
    }

    fn request_stream_test<TestFn, Fut>(test_object: Persona, test_fn: TestFn)
    where
        TestFn: FnOnce(PersonaProxy) -> Fut,
        Fut: Future<Output = Result<(), Error>>,
    {
        let mut executor = fasync::Executor::new().expect("Failed to create executor");
        let (server_chan, client_chan) = zx::Channel::create().expect("Failed to create channel");
        let proxy = PersonaProxy::new(fasync::Channel::from_channel(client_chan).unwrap());
        let request_stream =
            PersonaRequestStream::from_channel(fasync::Channel::from_channel(server_chan).unwrap());

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
    fn test_id() {
        assert_eq!(
            create_test_object().id(),
            &LocalPersonaId::new(TEST_PERSONA_ID)
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
    fn test_get_token_manager() {
        request_stream_test(create_test_object(), async move |proxy| {
            let (server_chan, _) = zx::Channel::create().unwrap();
            let token_manager = ServerEnd::new(server_chan);
            assert_eq!(
                await!(proxy.get_token_manager(&TEST_APPLICATION_URL, token_manager))?,
                Status::InternalError
            );
            Ok(())
        });
    }
}
