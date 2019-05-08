// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;

use serde_json::Value;
use std::sync::Arc;

use crate::setui::types::{SetUiMethod};
use crate::setui::facade::SetUiFacade;

/// Takes JSON-RPC method command and forwards to corresponding SetUi FIDL method.
pub async fn setui_method_to_fidl(
    method_name: String,
    args: Value,
    facade: Arc<SetUiFacade>,
) -> Result<Value, Error> {
    match method_name.parse()? {
        SetUiMethod::Mutate => await!(facade.mutate(args)),
    }
}
