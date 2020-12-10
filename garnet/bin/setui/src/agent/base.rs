// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::internal::agent::message::Receptor;
use crate::internal::event;
use crate::internal::switchboard;
use crate::message::base::MessengerType;
use crate::service_context::ServiceContextHandle;
use crate::switchboard::base::SettingType;
use anyhow::Error;
use async_trait::async_trait;
use core::fmt::Debug;
use futures::channel::mpsc::UnboundedSender;
use futures::future::BoxFuture;
use std::collections::HashSet;
use std::sync::Arc;
use thiserror::Error;

pub type AgentId = usize;

pub type GenerateAgent = Arc<dyn Fn(Context) + Send + Sync>;

pub type InvocationResult = Result<(), AgentError>;
pub type InvocationSender = UnboundedSender<Invocation>;

#[derive(Error, Debug, Clone)]
pub enum AgentError {
    #[error("Unhandled Lifespan")]
    UnhandledLifespan,
    #[error("Unexpected Error")]
    UnexpectedError,
}

/// Identification for the agent used for logging purposes.
#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub struct Descriptor {
    component: String,
}

impl Descriptor {
    /// Returns a `Descriptor` with the passed in argument as the component
    /// name.
    pub fn new(component: &str) -> Self {
        Self { component: component.to_string() }
    }
}

pub struct Context {
    pub receptor: Receptor,
    event_factory: event::message::Factory,
    publisher: event::Publisher,
    switchboard_messenger_factory: switchboard::message::Factory,
    pub available_components: HashSet<SettingType>,
}

impl Context {
    pub async fn new(
        receptor: Receptor,
        descriptor: Descriptor,
        switchboard_messenger_factory: switchboard::message::Factory,
        event_factory: event::message::Factory,
        available_components: HashSet<SettingType>,
    ) -> Self {
        let publisher = event::Publisher::create(
            &event_factory,
            MessengerType::Addressable(event::Address::Agent(descriptor)),
        )
        .await;
        Self {
            receptor,
            event_factory,
            publisher,
            switchboard_messenger_factory,
            available_components,
        }
    }

    /// Generates a new `Messenger` on the switchboard's `MessageHub`. Only
    /// top-level messages can be sent, not received, as the associated
    /// `Receptor` is discarded.
    pub async fn create_switchboard_messenger(
        &self,
    ) -> Result<switchboard::message::Messenger, switchboard::message::MessageError> {
        let (messenger, _) =
            self.switchboard_messenger_factory.create(MessengerType::Unbound).await?;

        Ok(messenger)
    }

    pub fn event_factory(&self) -> &event::message::Factory {
        &self.event_factory
    }

    pub fn get_publisher(&self) -> event::Publisher {
        self.publisher.clone()
    }
}

/// The scope of an agent's life. Initialization components should
/// only run at the beginning of the service. Service components follow
/// initialization and run for the duration of the service.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Lifespan {
    Initialization,
    Service,
}

/// Struct of information passed to the agent during each invocation.
#[derive(Clone, Debug)]
pub struct Invocation {
    pub lifespan: Lifespan,
    pub service_context: ServiceContextHandle,
}

/// Blueprint defines an interface provided to the authority for constructing
/// a given agent.
pub trait Blueprint {
    /// Returns the Agent descriptor to be associated with components used
    /// by this agent, such as logging.
    fn get_descriptor(&self) -> Descriptor;

    /// Uses the supplied context to create agent.
    fn create(&self, context: Context) -> BoxFuture<'static, ()>;
}

pub type BlueprintHandle = Arc<dyn Blueprint + Send + Sync>;

/// Entity for registering agents. It is responsible for signaling
/// Stages based on the specified lifespan.
#[async_trait]
pub trait Authority {
    async fn register(&mut self, blueprint: BlueprintHandle) -> Result<(), Error>;
}

#[macro_export]
macro_rules! blueprint_definition {
    ($component:expr, $create:expr) => {
        pub mod blueprint {
            #[allow(unused_imports)]
            use super::*;
            use crate::agent::base;
            use futures::future::BoxFuture;
            use std::sync::Arc;

            pub fn create() -> base::BlueprintHandle {
                Arc::new(BlueprintImpl)
            }

            struct BlueprintImpl;

            impl base::Blueprint for BlueprintImpl {
                fn get_descriptor(&self) -> base::Descriptor {
                    base::Descriptor::new($component)
                }

                fn create(&self, context: base::Context) -> BoxFuture<'static, ()> {
                    Box::pin(async move {
                        $create(context).await;
                    })
                }
            }
        }
    };
}
