// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::internal::agent::message::Receptor;
use crate::service_context::ServiceContextHandle;
use crate::switchboard::base::{SettingType, SwitchboardClient};
use anyhow::Error;
use async_trait::async_trait;
use core::fmt::Debug;
use futures::channel::mpsc::UnboundedSender;
use std::collections::HashSet;
use std::sync::Arc;
use thiserror::Error;

pub type AgentId = usize;

pub type GenerateAgent = Arc<dyn Fn(Receptor) + Send + Sync>;

pub type InvocationResult = Result<(), AgentError>;
pub type InvocationSender = UnboundedSender<Invocation>;

#[derive(Error, Debug, Clone)]
pub enum AgentError {
    #[error("Unhandled Lifespan")]
    UnhandledLifespan,
    #[error("Unexpected Error")]
    UnexpectedError,
}

/// The scope of an agent's life. Initialization components should
/// only run at the beginning of the service. Service components follow
/// initialization and run for the duration of the service.
#[derive(Clone, Debug)]
pub enum Lifespan {
    Initialization(InitializationContext),
    Service(RunContext),
}

#[derive(Clone, Debug)]
pub struct InitializationContext {
    pub available_components: HashSet<SettingType>,
    pub switchboard_client: SwitchboardClient,
}

impl InitializationContext {
    pub fn new(switchboard_client: SwitchboardClient, components: HashSet<SettingType>) -> Self {
        Self { available_components: components, switchboard_client: switchboard_client }
    }
}

#[derive(Clone, Debug)]
pub struct RunContext {
    pub switchboard_client: SwitchboardClient,
}

/// Struct of information passed to the agent during each invocation.
#[derive(Clone, Debug)]
pub struct Invocation {
    pub lifespan: Lifespan,
    pub service_context: ServiceContextHandle,
}

/// Entity for registering agents. It is responsible for signaling
/// Stages based on the specified lifespan.
#[async_trait]
pub trait Authority {
    async fn register(&mut self, generator: GenerateAgent) -> Result<(), Error>;
}
