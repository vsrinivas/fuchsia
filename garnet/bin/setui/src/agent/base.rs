// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::service_context::ServiceContext;
use failure::{format_err, Error};
use futures::channel::mpsc::UnboundedSender;
use futures::channel::oneshot::Sender;
use futures::lock::Mutex;
use parking_lot::RwLock;
use std::sync::Arc;

/// An InvocationTuple is sent to an agent for a given lifespan event. The first
/// element of the tuple is an Invocation, which provides context about the
/// lifespan and stage, along with a service context that should be used to
/// connect to external services. The second tuple element is a sender, meant
/// to acknowledge the invocation has been processed.
pub type InvocationAck = Result<(), Error>;
pub type InvocationSender = UnboundedSender<Invocation>;

/// The scope of an agent's life. Initialization components should
/// only run at the beginning of the service. Service components follow
/// initialization and run for the duration of the service.
#[derive(PartialEq, Debug, Eq, Hash, Clone, Copy)]
pub enum Lifespan {
  Initialization,
  Service,
}

/// Struct of information passed to the agent during each invocation.
#[derive(Clone)]
pub struct Invocation {
  pub lifespan: Lifespan,
  pub service_context: Arc<RwLock<ServiceContext>>,
  pub ack_sender: Arc<Mutex<Option<Sender<InvocationAck>>>>,
}

impl Invocation {
  pub async fn acknowledge(self, ack: InvocationAck) -> Result<(), Error> {
    match self.ack_sender.lock().await.take() {
      None => {
        return Err(format_err!("invocation already acknowledged"));
      }
      Some(sender) => {
        sender.send(ack).ok();
        return Ok(());
      }
    }
  }
}

/// Entity for registering agents. It is responsible for signaling
/// Stages based on the specified lifespan.
pub trait Authority {
  fn register(&mut self, lifespan: Lifespan, invoker: InvocationSender) -> Result<(), Error>;
}
