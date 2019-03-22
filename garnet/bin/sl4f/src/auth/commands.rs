// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{bail, Error};

use serde_json::{from_value, to_value, Value};
use std::sync::Arc;

use crate::auth::facade::AuthFacade;
use crate::auth::types::InjectAuthTokenRequest;

/// Takes ACTS method command and forwards to corresponding auth debug FIDL
/// method.
///
/// The InjectAuthToken expects |InjectAuthTokenRequest| serialized in args
/// and returns |InjectAuthTokenResult| enum.
pub async fn auth_method_to_fidl(
    method_name: String,
    args: Value,
    facade: Arc<AuthFacade>,
) -> Result<Value, Error> {
    match method_name.as_ref() {
        "InjectAuthToken" => {
            let request: InjectAuthTokenRequest = from_value(args)?;
            let result =
                await!(facade.inject_auth_token(request.user_profile_info, request.credential,))?;
            Ok(to_value(result)?)
        }
        _ => bail!("Invalid Auth Facade method: {:?}", method_name),
    }
}
