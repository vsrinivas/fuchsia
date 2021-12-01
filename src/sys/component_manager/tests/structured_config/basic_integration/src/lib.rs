// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use cm_rust::FidlIntoNative;
use fidl::encoding::decode_persistent;
use fidl_fuchsia_component_config::ValuesData;

#[fuchsia::test]
fn manually_resolve_structured_config() {
    // read the config declaration
    let manifest_raw = std::fs::read("/pkg/meta/basic_config_receiver.cm").unwrap();
    let manifest: fidl_fuchsia_sys2::ComponentDecl = decode_persistent(&manifest_raw[..]).unwrap();
    let manifest = manifest.fidl_into_native();
    let _config = manifest.config.as_ref().unwrap();

    // read the value file
    let value_file_raw = std::fs::read("/pkg/meta/basic_config_receiver.cvf").unwrap();
    let _value_file: ValuesData = decode_persistent(&value_file_raw[..]).unwrap();

    // TODO check that the decl and value file match
}
