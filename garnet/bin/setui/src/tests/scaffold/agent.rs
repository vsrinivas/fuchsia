// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::agent;
use futures::future::BoxFuture;
use std::sync::Arc;

#[derive(Clone)]
pub enum Generate {
    Sync(Arc<dyn Fn(agent::Context) + Send + Sync>),
    Async(Arc<dyn Fn(agent::Context) -> BoxFuture<'static, ()> + Send + Sync>),
}

/// This blueprint allows tests to specify either an asynchronous or syncronous
/// agent creation method. Note that the descriptor must be unique for the test
/// scope or else the MessageHub will fail on the name collision.
pub struct Blueprint {
    generate: Generate,
}

impl Blueprint {
    pub fn new(generate: Generate) -> Self {
        Self { generate }
    }
}

impl agent::Blueprint for Blueprint {
    fn debug_id(&self) -> &'static str {
        "TestAgent"
    }

    fn create(&self, context: agent::Context) -> BoxFuture<'static, ()> {
        match &self.generate {
            Generate::Sync(func) => {
                let func = func.clone();
                Box::pin(async move {
                    (func)(context);
                })
            }
            Generate::Async(func) => (func)(context),
        }
    }
}
