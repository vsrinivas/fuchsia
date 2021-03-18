// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The [`StorageAgent`] is responsible for all reads and writes to storage for the
//! settings service.

use crate::agent::{self, AgentError, Context, Invocation, InvocationResult, Payload};
use crate::event::Publisher;
use crate::handler::device_storage::DeviceStorageFactory;
use crate::message::base::MessageEvent;
use crate::service;
use fuchsia_async as fasync;
use futures::future::BoxFuture;
use futures::StreamExt;
use std::sync::Arc;

pub struct Blueprint<T>
where
    T: DeviceStorageFactory,
{
    storage_factory: Arc<T>,
}

impl<T> Blueprint<T>
where
    T: DeviceStorageFactory,
{
    pub fn new(storage_factory: Arc<T>) -> Self {
        Self { storage_factory }
    }
}

impl<T> agent::Blueprint for Blueprint<T>
where
    T: DeviceStorageFactory + Send + Sync + 'static,
{
    fn create(&self, context: Context) -> BoxFuture<'static, ()> {
        let storage_factory = Arc::clone(&self.storage_factory);
        Box::pin(async move {
            StorageAgent::create(context, storage_factory).await;
        })
    }
}

// TODO(fxbug.dev/67371) Remove allow when handler implemented.
#[allow(dead_code)]
pub struct StorageAgent<T>
where
    T: DeviceStorageFactory + Send + Sync + 'static,
{
    messenger: service::message::Messenger,
    event_publisher: Publisher,
    storage_factory: Arc<T>,
}

impl<T> StorageAgent<T>
where
    T: DeviceStorageFactory + Send + Sync + 'static,
{
    async fn create(mut context: Context, storage_factory: Arc<T>) {
        let event_publisher = context.get_publisher();
        let messenger = context.create_messenger().await.expect("should acquire messenger");
        let mut storage_agent = StorageAgent { messenger, event_publisher, storage_factory };

        fasync::Task::spawn(async move {
            while let Some(event) = context.receptor.next().await {
                if let MessageEvent::Message(
                    service::Payload::Agent(Payload::Invocation(invocation)),
                    client,
                ) = event
                {
                    client
                        .reply(service::Payload::Agent(Payload::Complete(
                            storage_agent.handle(invocation).await,
                        )))
                        .send();
                }
            }
        })
        .detach();
    }

    async fn handle(&mut self, _invocation: Invocation) -> InvocationResult {
        // TODO(fxbug.dev/67371) Implement storage agent.
        Err(AgentError::UnhandledLifespan)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::agent::Lifespan;
    use crate::handler::device_storage::testing::InMemoryStorageFactory;
    use crate::message::base::MessengerType;
    use crate::service_context::ServiceContext;

    // TODO(fxbug.dev/67371) Remove when agent implemented.
    #[fuchsia_async::run_until_stalled(test)]
    async fn handle_invocation_always_returns_unhandled() {
        let messenger_factory = service::message::create_hub();
        let (messenger, _) =
            messenger_factory.create(MessengerType::Unbound).await.expect("messenger");
        let mut agent = StorageAgent {
            messenger,
            event_publisher: Publisher::create(&messenger_factory, MessengerType::Unbound).await,
            storage_factory: Arc::new(InMemoryStorageFactory::new()),
        };

        let service_context = ServiceContext::create(None, None);
        let result = agent
            .handle(Invocation {
                lifespan: Lifespan::Initialization,
                service_context: Arc::clone(&service_context),
            })
            .await;
        assert_eq!(result, Err(AgentError::UnhandledLifespan));

        let result =
            agent.handle(Invocation { lifespan: Lifespan::Service, service_context }).await;
        assert_eq!(result, Err(AgentError::UnhandledLifespan));
    }
}
