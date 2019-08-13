// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use fidl::endpoints::{DiscoverableService, Proxy};

use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_service;

use fuchsia_zircon as zx;

type GenerateService = Box<dyn Fn(&str, zx::Channel) -> Result<(), Error> + Send + Sync>;

/// A wrapper around service operations, allowing redirection to a nested
/// environment.
pub struct ServiceContext {
    generate_service: Option<GenerateService>,
}

impl ServiceContext {
    pub fn new(generate_service: Option<GenerateService>) -> Self {
        Self { generate_service: generate_service }
    }

    pub fn connect<S: DiscoverableService>(&self) -> Result<S::Proxy, Error> {
        if let Some(generate_service) = &self.generate_service {
            let (client, server) = zx::Channel::create()?;
            (generate_service)(S::NAME, server)?;
            return Ok(S::Proxy::from_channel(fasync::Channel::from_channel(client)?));
        } else {
            return connect_to_service::<S>();
        }
    }
}
