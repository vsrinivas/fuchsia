// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    cm_fidl_validator, fidl_fuchsia_mem as fmem, fidl_fuchsia_sys2 as fsys,
    fuchsia_async as fasync,
    futures::{lock::Mutex, TryStreamExt},
    log::*,
    std::{collections::HashMap, sync::Arc},
};

const RESOLVER_SCHEME: &'static str = "fuchsia-component-test-registry";

pub struct Registry {
    next_unique_component_id: Mutex<u64>,
    component_decls: Mutex<HashMap<String, fsys::ComponentDecl>>,
}

impl Registry {
    pub fn new() -> Arc<Self> {
        Arc::new(Self {
            next_unique_component_id: Mutex::new(0),
            component_decls: Mutex::new(HashMap::new()),
        })
    }

    pub async fn validate_and_register(
        self: &Arc<Self>,
        decl: fsys::ComponentDecl,
    ) -> Result<String, cm_fidl_validator::ErrorList> {
        cm_fidl_validator::validate(&decl)?;

        let mut next_unique_component_id_guard = self.next_unique_component_id.lock().await;
        let mut component_decls_guard = self.component_decls.lock().await;

        let url = format!("{}://{}", RESOLVER_SCHEME, *next_unique_component_id_guard);
        *next_unique_component_id_guard += 1;
        component_decls_guard.insert(url.clone(), decl);
        Ok(url)
    }

    pub fn run_resolver_service(self: &Arc<Self>, stream: fsys::ComponentResolverRequestStream) {
        let self_ref = self.clone();
        fasync::Task::local(async move {
            if let Err(e) = self_ref.handle_resolver_request_stream(stream).await {
                warn!("error encountered while running resolver service: {:?}", e);
            }
        })
        .detach();
    }

    async fn handle_resolver_request_stream(
        self: &Arc<Self>,
        mut stream: fsys::ComponentResolverRequestStream,
    ) -> Result<(), Error> {
        while let Some(req) = stream.try_next().await? {
            match req {
                fsys::ComponentResolverRequest::Resolve { component_url, responder } => {
                    if let Some(decl) = self.component_decls.lock().await.get(&component_url) {
                        responder.send(&mut Ok(fsys::Component {
                            resolved_url: Some(component_url),
                            decl: Some(encode(decl.clone())?),
                            package: None,
                            ..fsys::Component::EMPTY
                        }))?;
                    } else {
                        responder.send(&mut Err(fsys::ResolverError::ManifestNotFound))?;
                    }
                }
            }
        }
        Ok(())
    }
}

fn encode(mut component_decl: fsys::ComponentDecl) -> Result<fmem::Data, Error> {
    Ok(fmem::Data::Bytes(
        fidl::encoding::encode_persistent(&mut component_decl)
            .context("failed to encode ComponentDecl")?,
    ))
}
