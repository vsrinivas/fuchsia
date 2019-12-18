// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_mem::Buffer,
    fidl_fuchsia_modular::{
        EntityRequest, EntityRequestStream, EntityResolverRequest, EntityResolverRequestStream,
        EntityWriteStatus,
    },
    fuchsia_async as fasync,
    fuchsia_syslog::macros::*,
    fuchsia_zircon as zx,
    futures::prelude::*,
    std::collections::HashMap,
};

#[derive(Clone)]
pub struct FakeEntityData {
    pub types: Vec<String>,
    pub data: String,
}

impl FakeEntityData {
    pub fn new(types: Vec<String>, data: &str) -> Self {
        FakeEntityData { types, data: data.to_string() }
    }
}

pub struct FakeEntityResolver {
    entities: HashMap<String, FakeEntityData>,
}

impl FakeEntityResolver {
    pub fn new() -> Self {
        FakeEntityResolver { entities: HashMap::new() }
    }

    pub fn register_entity(&mut self, reference: &str, data: FakeEntityData) {
        self.entities.insert(reference.to_string(), data);
    }

    pub fn spawn(self, mut stream: EntityResolverRequestStream) {
        fasync::spawn(
            async move {
                while let Some(request) =
                    stream.try_next().await.context("error running entity resolver")?
                {
                    match request {
                        EntityResolverRequest::ResolveEntity {
                            entity_reference,
                            entity_request,
                            ..
                        } => {
                            let stream = entity_request.into_stream()?;
                            if let Some(entity) = self.entities.get(&entity_reference) {
                                FakeEntityServer::new(&entity_reference, entity.clone())
                                    .spawn(stream);
                            }
                        }
                    }
                }
                Ok(())
            }
            .unwrap_or_else(|e: Error| fx_log_err!("error serving fake entity resolver: {:?}", e)),
        );
    }
}

pub struct FakeEntityServer {
    entity_reference: String,
    entity: FakeEntityData,
}

impl FakeEntityServer {
    pub fn new(reference: &str, entity: FakeEntityData) -> Self {
        FakeEntityServer { entity_reference: reference.to_string(), entity }
    }

    pub fn spawn(mut self, mut stream: EntityRequestStream) {
        fasync::spawn(
            async move {
                while let Some(request) =
                    stream.try_next().await.context("error running entity server")?
                {
                    match request {
                        EntityRequest::GetTypes { responder } => {
                            responder.send(&mut self.entity.types.iter().map(|s| s.as_ref()))?;
                        }
                        EntityRequest::GetData { responder, .. } => {
                            let data = self.entity.data.as_bytes();
                            let vmo = zx::Vmo::create(data.len() as u64)?;
                            vmo.write(&data, 0)?;
                            responder.send(Some(&mut Buffer { vmo, size: data.len() as u64 }))?;
                        }
                        EntityRequest::WriteData { responder, .. } => {
                            responder.send(EntityWriteStatus::ReadOnly)?;
                        }
                        EntityRequest::GetReference { responder } => {
                            responder.send(&mut self.entity_reference)?;
                        }
                        EntityRequest::Watch { .. } => {
                            // Not implemented.
                        }
                    }
                }
                Ok(())
            }
            .unwrap_or_else(|e: Error| fx_log_err!("error serving fake entity: {:?}", e)),
        );
    }
}
