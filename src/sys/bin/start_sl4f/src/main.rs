// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fuchsia_component as fcomponent;
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_protocol_at_path;

// Note: The protocol "sl4f.Binder" is created in sl4f.cml
const SL4F_EXPOSED: &str = "/svc/sl4f.Binder";

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    connect_to_protocol_at_path::<fcomponent::BinderMarker>(SL4F_EXPOSED)
        .expect("failed to connect to sl4f.Binder");
    Ok(())
}
