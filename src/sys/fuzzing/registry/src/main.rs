// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod registry;

use {
    fidl_fuchsia_fuzzer as fuzz, fuchsia_component::server::ServiceFs, futures::StreamExt,
    std::rc::Rc,
};

enum IncomingService {
    FuzzRegistry(fuzz::RegistryRequestStream),
    FuzzRegistrar(fuzz::RegistrarRequestStream),
}

#[fuchsia::main(logging = true)]
async fn main() -> anyhow::Result<()> {
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(IncomingService::FuzzRegistry);
    fs.dir("svc").add_fidl_service(IncomingService::FuzzRegistrar);
    fs.take_and_serve_directory_handle()?;
    const MAX_CONCURRENT: usize = 100;
    let registry_rc = Rc::new(registry::FuzzRegistry::new());
    fs.for_each_concurrent(MAX_CONCURRENT, |incoming_service| async {
        let registry = Rc::clone(&registry_rc);
        match incoming_service {
            IncomingService::FuzzRegistry(stream) => registry.serve_registry(stream).await,
            IncomingService::FuzzRegistrar(stream) => registry.serve_registrar(stream).await,
        };
    })
    .await;
    Ok(())
}
