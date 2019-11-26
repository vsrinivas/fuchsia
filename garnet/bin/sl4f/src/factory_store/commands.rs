// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::server::Facade;
use failure::Error;
use futures::future::{FutureExt, LocalBoxFuture};
use serde_json::Value;

use crate::factory_store::facade::FactoryStoreFacade;
use crate::factory_store::types::FactoryStoreMethod;

impl Facade for FactoryStoreFacade {
    fn handle_request(
        &self,
        method: String,
        args: Value,
    ) -> LocalBoxFuture<'_, Result<Value, Error>> {
        factory_store_method_to_fidl(method, args, self).boxed_local()
    }
}

/// Takes JSON-RPC method command and forwards to corresponding FactoryStoreProvider FIDL methods.
async fn factory_store_method_to_fidl(
    method_name: String,
    args: Value,
    facade: &FactoryStoreFacade,
) -> Result<Value, Error> {
    match method_name.parse()? {
        FactoryStoreMethod::ReadFile => facade.read_file(args).await,
        FactoryStoreMethod::ListFiles => facade.list_files(args).await,
    }
}
