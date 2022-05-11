// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Context as _;
use cm_rust::{ComponentDecl, FidlIntoNative};
use fidl::encoding::decode_persistent;
use fidl_fuchsia_component_decl as fdecl;
use std::{fs, path::Path};

pub fn load_manifest(cm: impl AsRef<Path>) -> anyhow::Result<ComponentDecl> {
    let cm_raw = fs::read(cm).context("reading component manifest")?;
    let component: fdecl::Component =
        decode_persistent(&cm_raw).context("decoding component manifest")?;
    Ok(component.fidl_into_native())
}
