// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::service_context::GenerateService;
use crate::tests::fakes::base::Service;
use anyhow::{format_err, Error};
use fuchsia_zircon as zx;
use futures::executor::block_on;
use futures::lock::Mutex;
use std::sync::Arc;

/// A helper class that gathers services through registration and directs
/// the appropriate channels to them.
pub struct ServiceRegistry {
    services: Vec<Arc<Mutex<dyn Service + Send + Sync>>>,
}

impl ServiceRegistry {
    pub fn create() -> Arc<Mutex<ServiceRegistry>> {
        return Arc::new(Mutex::new(ServiceRegistry { services: Vec::new() }));
    }

    pub fn register_service(&mut self, service: Arc<Mutex<dyn Service + Send + Sync>>) {
        self.services.push(service);
    }

    fn service_channel(&self, service_name: &str, channel: zx::Channel) -> Result<(), Error> {
        for service_handle in self.services.iter() {
            let service = block_on(service_handle.lock());
            if service.can_handle_service(service_name) {
                return service.process_stream(service_name, channel);
            }
        }

        return Err(format_err!("channel not handled for service: {}", service_name));
    }

    pub fn serve(registry_handle: Arc<Mutex<ServiceRegistry>>) -> Option<GenerateService> {
        return Some(Box::new(move |service_name: &str, channel: zx::Channel| {
            return block_on(registry_handle.lock()).service_channel(service_name, channel);
        }));
    }
}
