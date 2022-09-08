// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{common::AccountLifetime, inspect},
    account_common::PersonaId,
    anyhow::Error,
    fidl_fuchsia_identity_account::{
        AuthState, AuthTargetRegisterAuthListenerRequest, Error as ApiError, Lifetime,
        PersonaRequest, PersonaRequestStream,
    },
    fuchsia_inspect::{Node, NumericProperty},
    futures::prelude::*,
    identity_common::{cancel_or, TaskGroup, TaskGroupCancel},
    std::sync::Arc,
    tracing::warn,
};

/// The context that a particular request to a Persona should be executed in, capturing
/// information that was supplied upon creation of the channel.
pub struct PersonaContext {
    // Note: The per-channel persona context is currently empty. It was needed in the past for
    // authentication UI and is likely to be needed again in the future as the API expands.
}

/// Information about one of the Personae withing the Account that this AccountHandler instance is
/// responsible for.
///
/// This state is only available once the Handler has been initialized to a particular account via
/// the AccountHandlerControl channel.
// TODO(dnordstrom): Factor out items that are accessed by both account and persona into its own
// type so they don't need to be individually copied or Arc-wrapped. Here, `lifetime` and
// `account_id` are candidates.
pub struct Persona {
    /// An identifier for this persona.
    id: PersonaId,

    /// Lifetime for this persona's account (ephemeral or persistent with a path).
    lifetime: Arc<AccountLifetime>,

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
        id: PersonaId,
        lifetime: Arc<AccountLifetime>,
        task_group: TaskGroup,
        inspect_parent: &Node,
    ) -> Persona {
        let persona_inspect = inspect::Persona::new(inspect_parent, &id);
        Self { id, lifetime, task_group, inspect: persona_inspect }
    }

    /// Returns the identifier for this persona.
    pub fn id(&self) -> &PersonaId {
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
        _context: &'a PersonaContext,
        req: PersonaRequest,
    ) -> Result<(), fidl::Error> {
        match req {
            PersonaRequest::GetLifetime { responder } => {
                let response = self.get_lifetime();
                responder.send(response)?;
            }
            PersonaRequest::GetAuthState { responder } => {
                let mut response = self.get_auth_state();
                responder.send(&mut response)?;
            }
            PersonaRequest::RegisterAuthListener { payload, responder } => {
                let mut response = self.register_auth_listener(payload);
                responder.send(&mut response)?;
            }
        }
        Ok(())
    }

    fn get_lifetime(&self) -> Lifetime {
        Lifetime::from(self.lifetime.as_ref())
    }

    fn get_auth_state(&self) -> Result<AuthState, ApiError> {
        // TODO(jsankey): Return real authentication state once authenticators exist to create it.
        Err(ApiError::UnsupportedOperation)
    }

    fn register_auth_listener(
        &self,
        _payload: AuthTargetRegisterAuthListenerRequest,
    ) -> Result<(), ApiError> {
        // TODO(jsankey): Implement this method.
        warn!("RegisterAuthListener not yet implemented");
        Err(ApiError::UnsupportedOperation)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_util::*;
    use fidl::endpoints::{create_endpoints, create_proxy_and_stream};
    use fidl_fuchsia_identity_account::{
        AuthChangeGranularity, AuthTargetRegisterAuthListenerRequest, PersonaMarker, PersonaProxy,
    };
    use fuchsia_async as fasync;
    use fuchsia_inspect::Inspector;
    use std::path::PathBuf;

    fn create_persona() -> Persona {
        let inspector = Inspector::new();
        Persona::new(
            TEST_PERSONA_ID.clone(),
            Arc::new(AccountLifetime::Persistent { account_dir: PathBuf::from("/nowhere") }),
            TaskGroup::new(),
            inspector.root(),
        )
    }

    fn create_ephemeral_persona() -> Persona {
        let inspector = Inspector::new();
        Persona::new(
            TEST_PERSONA_ID.clone(),
            Arc::new(AccountLifetime::Ephemeral),
            TaskGroup::new(),
            inspector.root(),
        )
    }

    async fn run_test<TestFn, Fut>(test_object: Persona, test_fn: TestFn)
    where
        TestFn: FnOnce(PersonaProxy) -> Fut,
        Fut: Future<Output = Result<(), Error>>,
    {
        let (persona_proxy, request_stream) = create_proxy_and_stream::<PersonaMarker>().unwrap();
        let persona_context = PersonaContext {};

        let task_group = TaskGroup::new();
        task_group
            .spawn(|cancel| async move {
                test_object
                    .handle_requests_from_stream(&persona_context, request_stream, cancel)
                    .await
                    .unwrap_or_else(|err| panic!("Fatal error handling test request: {:?}", err));
            })
            .await
            .expect("Unable to spawn task");
        test_fn(persona_proxy).await.expect("Failed running test fn.")
    }

    #[fasync::run_until_stalled(test)]
    async fn test_id() {
        assert_eq!(create_persona().id(), &*TEST_PERSONA_ID);
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_lifetime_persistent() {
        assert_eq!(create_persona().get_lifetime(), Lifetime::Persistent);
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_lifetime_ephemeral() {
        assert_eq!(create_ephemeral_persona().get_lifetime(), Lifetime::Ephemeral);
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_auth_state() {
        run_test(create_persona(), |proxy| async move {
            assert_eq!(proxy.get_auth_state().await?, Err(ApiError::UnsupportedOperation));
            Ok(())
        })
        .await;
    }

    #[fasync::run_until_stalled(test)]
    async fn test_register_auth_listener() {
        run_test(create_persona(), |proxy| async move {
            let (auth_listener_client_end, _) = create_endpoints().unwrap();
            assert_eq!(
                proxy
                    .register_auth_listener(AuthTargetRegisterAuthListenerRequest {
                        listener: Some(auth_listener_client_end),
                        initial_state: Some(true),
                        granularity: Some(AuthChangeGranularity {
                            summary_changes: Some(true),
                            ..AuthChangeGranularity::EMPTY
                        }),
                        ..AuthTargetRegisterAuthListenerRequest::EMPTY
                    })
                    .await?,
                Err(ApiError::UnsupportedOperation)
            );
            Ok(())
        })
        .await;
    }
}
