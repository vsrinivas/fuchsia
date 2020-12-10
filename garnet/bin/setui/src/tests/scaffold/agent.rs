// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::agent::base;
use futures::future::BoxFuture;
use std::sync::Arc;

#[derive(Clone)]
pub enum Generate {
    Sync(Arc<dyn Fn(base::Context) + Send + Sync>),
    Async(Arc<dyn Fn(base::Context) -> BoxFuture<'static, ()> + Send + Sync>),
}

/// This blueprint allows tests to specify either an asynchronous or syncronous
/// agent creation method. Note that the descriptor must be unique for the test
/// scope or else the MessageHub will fail on the name collision.
pub struct Blueprint {
    generate: Generate,
    descriptor: base::Descriptor,
}

impl Blueprint {
    pub fn new(generate: Generate, component: &str) -> Self {
        Self { generate: generate, descriptor: base::Descriptor::new(component) }
    }
}

impl base::Blueprint for Blueprint {
    fn get_descriptor(&self) -> base::Descriptor {
        self.descriptor.clone()
    }

    fn create(&self, context: base::Context) -> BoxFuture<'static, ()> {
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
