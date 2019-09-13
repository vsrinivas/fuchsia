// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;

use serde_json::Value;
use std::sync::Arc;

use crate::factory_store::facade::FactoryStoreFacade;
use crate::factory_store::types::FactoryStoreMethod;

/// Takes JSON-RPC method command and forwards to corresponding FactoryStoreProvider FIDL methods.
pub async fn factory_store_method_to_fidl(
    method_name: String,
    args: Value,
    facade: Arc<FactoryStoreFacade>,
) -> Result<Value, Error> {
    match method_name.parse()? {
        FactoryStoreMethod::ReadFile => facade.read_file(args).await,
        FactoryStoreMethod::ListFiles => facade.list_files(args).await,
    }
}
