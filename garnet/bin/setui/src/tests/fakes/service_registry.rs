// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::service_context::GenerateService;
use crate::tests::fakes::base::Service;
use anyhow::{format_err, Error};
use fuchsia_zircon as zx;
use futures::future::BoxFuture;
use futures::lock::Mutex;
use std::sync::Arc;

pub type ServiceRegistryHandle = Arc<Mutex<ServiceRegistry>>;

/// A helper class that gathers services through registration and directs
/// the appropriate channels to them.
pub struct ServiceRegistry {
    services: Vec<Arc<Mutex<dyn Service + Send + Sync>>>,
}

impl ServiceRegistry {
    pub fn create() -> ServiceRegistryHandle {
        return Arc::new(Mutex::new(ServiceRegistry { services: Vec::new() }));
    }

    pub fn register_service(&mut self, service: Arc<Mutex<dyn Service + Send + Sync>>) {
        self.services.push(service);
    }

    async fn service_channel(&self, service_name: &str, channel: zx::Channel) -> Result<(), Error> {
        for service_handle in self.services.iter() {
            let mut service = service_handle.lock().await;
            if service.can_handle_service(service_name) {
                return service.process_stream(service_name, channel);
            }
        }

        return Err(format_err!("channel not handled for service: {}", service_name));
    }

    pub fn serve(registry_handle: ServiceRegistryHandle) -> GenerateService {
        return Box::new(
            move |service_name: &str, channel: zx::Channel| -> BoxFuture<'_, Result<(), Error>> {
                let registry_handle_clone = registry_handle.clone();
                let service_name_clone = String::from(service_name);

                Box::pin(async move {
                    return registry_handle_clone
                        .lock()
                        .await
                        .service_channel(service_name_clone.as_str(), channel)
                        .await;
                })
            },
        );
    }
}
