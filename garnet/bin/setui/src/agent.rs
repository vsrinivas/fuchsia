// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::SettingType;
use crate::internal::agent::message::Receptor;
use crate::internal::event;
use crate::message::base::MessengerType;
use crate::monitor;
use crate::service;
use crate::service_context::ServiceContextHandle;

use anyhow::Error;
use async_trait::async_trait;
use futures::future::BoxFuture;
use std::collections::HashSet;
use std::sync::Arc;
use thiserror::Error;

/// Agent for watching the camera3 status.
pub mod camera_watcher;

/// Agent for handling media button input.
pub mod media_buttons;

/// This mod provides a concrete implementation of the agent authority.
pub mod authority_impl;

/// Agent for rehydrating actions for restore.
pub mod restore_agent;

/// Agent for capturing requests.
pub mod inspect;

/// Earcons.
pub mod earcons;

#[derive(Error, Debug, Clone)]
pub enum AgentError {
    #[error("Unhandled Lifespan")]
    UnhandledLifespan,
    #[error("Unexpected Error")]
    UnexpectedError,
}

pub type InvocationResult = Result<(), AgentError>;

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

/// TODO(fxbug.dev/68659): Add documentation.
pub struct Context {
    pub receptor: Receptor,
    event_factory: event::message::Factory,
    publisher: event::Publisher,
    pub messenger_factory: service::message::Factory,
    pub available_components: HashSet<SettingType>,
    pub resource_monitor_actor: Option<monitor::environment::Actor>,
}

impl Context {
    pub async fn new(
        receptor: Receptor,
        descriptor: Descriptor,
        messenger_factory: service::message::Factory,
        event_factory: event::message::Factory,
        available_components: HashSet<SettingType>,
        resource_monitor_actor: Option<monitor::environment::Actor>,
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
            messenger_factory,
            available_components,
            resource_monitor_actor,
        }
    }

    /// Generates a new `Messenger` on the service `MessageHub`. Only
    /// top-level messages can be sent, not received, as the associated
    /// `Receptor` is discarded.
    pub async fn create_messenger(
        &self,
    ) -> Result<service::message::Messenger, service::message::MessageError> {
        Ok(self.messenger_factory.create(MessengerType::Unbound).await?.0)
    }

    pub fn event_factory(&self) -> &event::message::Factory {
        &self.event_factory
    }

    pub fn get_publisher(&self) -> event::Publisher {
        self.publisher.clone()
    }
}

#[macro_export]
macro_rules! blueprint_definition {
    ($component:expr, $create:expr) => {
        pub mod blueprint {
            #[allow(unused_imports)]
            use super::*;
            use crate::agent::{Blueprint, BlueprintHandle, Context, Descriptor};
            use futures::future::BoxFuture;
            use std::sync::Arc;

            pub fn create() -> BlueprintHandle {
                Arc::new(BlueprintImpl)
            }

            struct BlueprintImpl;

            impl Blueprint for BlueprintImpl {
                fn get_descriptor(&self) -> Descriptor {
                    Descriptor::new($component)
                }

                fn create(&self, context: Context) -> BoxFuture<'static, ()> {
                    Box::pin(async move {
                        $create(context).await;
                    })
                }
            }
        }
    };
}
