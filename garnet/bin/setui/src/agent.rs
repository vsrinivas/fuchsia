// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::SettingType;
use crate::event;
use crate::message::base::MessengerType;
use crate::monitor;
use crate::payload_convert;
use crate::policy::PolicyType;
use crate::service;
use crate::service::message::Receptor;
use crate::service_context::ServiceContext;

use futures::future::BoxFuture;
use std::collections::HashSet;
use std::fmt::Debug;
use std::sync::Arc;
use thiserror::Error;

/// Agent for watching the camera3 status.
pub(crate) mod camera_watcher;

/// Agent for handling media button input.
pub(crate) mod media_buttons;

/// This mod provides a concrete implementation of the agent authority.
pub(crate) mod authority;

/// Agent for rehydrating actions for restore.
pub(crate) mod restore_agent;

/// Agent for managing access to storage.
pub(crate) mod storage_agent;

/// Agent for capturing requests.
pub(crate) mod inspect;

/// Earcons.
pub(crate) mod earcons;

/// Agent for capturing policy state from messages from the message hub to
/// policy proxies.
pub(crate) mod inspect_policy;

/// Agent for capturing setting values of messages between proxies and setting
/// handlers.
pub(crate) mod inspect_setting_data;

#[derive(Error, Debug, Clone, Copy, PartialEq)]
pub enum AgentError {
    #[error("Unhandled Lifespan")]
    UnhandledLifespan,
    #[error("Unexpected Error")]
    UnexpectedError,
}

pub(crate) type InvocationResult = Result<(), AgentError>;

/// The scope of an agent's life. Initialization components should
/// only run at the beginning of the service. Service components follow
/// initialization and run for the duration of the service.
#[derive(Clone, Copy, Debug, PartialEq)]
pub(crate) enum Lifespan {
    Initialization,
    Service,
}

/// Struct of information passed to the agent during each invocation.
#[derive(Clone)]
pub struct Invocation {
    pub(crate) lifespan: Lifespan,
    pub(crate) service_context: Arc<ServiceContext>,
}

impl Debug for Invocation {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Invocation").field("lifespan", &self.lifespan).finish_non_exhaustive()
    }
}

impl PartialEq for Invocation {
    fn eq(&self, other: &Self) -> bool {
        self.lifespan == other.lifespan
    }
}

/// Blueprint defines an interface provided to the authority for constructing
/// a given agent.
pub trait Blueprint {
    /// Returns a debug id that can be used during error reporting.
    fn debug_id(&self) -> &'static str;

    /// Uses the supplied context to create agent.
    fn create(&self, context: Context) -> BoxFuture<'static, ()>;
}

pub type BlueprintHandle = Arc<dyn Blueprint + Send + Sync>;

/// TODO(fxbug.dev/68659): Add documentation.
pub struct Context {
    pub receptor: Receptor,
    publisher: event::Publisher,
    pub delegate: service::message::Delegate,
    pub(crate) available_components: HashSet<SettingType>,
    pub available_policies: HashSet<PolicyType>,
    pub resource_monitor_actor: Option<monitor::environment::Actor>,
}

impl Context {
    pub(crate) async fn new(
        receptor: Receptor,
        delegate: service::message::Delegate,
        available_components: HashSet<SettingType>,
        available_policies: HashSet<PolicyType>,
        resource_monitor_actor: Option<monitor::environment::Actor>,
    ) -> Self {
        let publisher = event::Publisher::create(&delegate, MessengerType::Unbound).await;
        Self {
            receptor,
            publisher,
            delegate,
            available_components,
            available_policies,
            resource_monitor_actor,
        }
    }

    /// Generates a new `Messenger` on the service `MessageHub`. Only
    /// top-level messages can be sent, not received, as the associated
    /// `Receptor` is discarded.
    async fn create_messenger(
        &self,
    ) -> Result<service::message::Messenger, service::message::MessageError> {
        Ok(self.delegate.create(MessengerType::Unbound).await?.0)
    }

    pub(crate) fn get_publisher(&self) -> event::Publisher {
        self.publisher.clone()
    }
}

#[macro_export]
macro_rules! blueprint_definition {
    ($component:literal, $create:expr) => {
        pub(crate) mod blueprint {
            #[allow(unused_imports)]
            use super::*;
            use crate::agent::{Blueprint, BlueprintHandle, Context};
            use futures::future::BoxFuture;
            use std::sync::Arc;

            pub(crate) fn create() -> BlueprintHandle {
                Arc::new(BlueprintImpl)
            }

            struct BlueprintImpl;

            impl Blueprint for BlueprintImpl {
                fn debug_id(&self) -> &'static str {
                    $component
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

#[derive(Clone, Debug, PartialEq)]
pub enum Payload {
    Invocation(Invocation),
    Complete(InvocationResult),
}

payload_convert!(Agent, Payload);
