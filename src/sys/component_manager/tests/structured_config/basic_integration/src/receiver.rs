// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::encoding::decode_persistent;
use fidl_fuchsia_component_config::ValuesData;
use fidl_test_structuredconfig_receiver::{
    ConfigReceiverPuppetRequest, ConfigReceiverPuppetRequestStream, ReceiverConfig,
};
use fuchsia_component::server::ServiceFs;
use fuchsia_runtime::{take_startup_handle, HandleInfo, HandleType};
use fuchsia_zircon as zx;
use futures::StreamExt;

enum IncomingRequest {
    Puppet(ConfigReceiverPuppetRequestStream),
}

#[fuchsia::component]
async fn main() {
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(IncomingRequest::Puppet);
    fs.take_and_serve_directory_handle().unwrap();
    fs.for_each_concurrent(None, |request: IncomingRequest| async move {
        match request {
            IncomingRequest::Puppet(mut reqs) => {
                while let Some(Ok(req)) = reqs.next().await {
                    match req {
                        ConfigReceiverPuppetRequest::GetConfig { responder } => {
                            responder.send(&mut received_config()).unwrap()
                        }
                    }
                }
            }
        }
    })
    .await;
}

// TODO(https://fxbug.dev/86136) replace with a generated library
fn received_config() -> ReceiverConfig {
    let config_vmo: zx::Vmo = take_startup_handle(HandleInfo::new(HandleType::ConfigVmo, 0))
        .expect("must have been provided with a config vmo")
        .into();
    let config_size = config_vmo.get_content_size().expect("must be able to read vmo content size");
    assert_ne!(config_size, 0, "config vmo must be non-empty");

    let mut config_bytes = Vec::new();
    config_bytes.resize(config_size as usize, 0);
    config_vmo.read(&mut config_bytes, 0).expect("must be able to read config vmo");

    let checksum_length = u16::from_le_bytes([config_bytes[0], config_bytes[1]]) as usize;
    let fidl_start = 2 + checksum_length;
    let observed_checksum = &config_bytes[2..fidl_start];

    let expected_checksum = {
        let value_file_raw = std::fs::read("/pkg/meta/basic_config_receiver.cvf").unwrap();
        let value_file: ValuesData = decode_persistent(&value_file_raw[..]).unwrap();
        // TODO(https://fxbug.dev/86136) this should be directly in the generated library's source
        value_file.declaration_checksum.unwrap()
    };
    assert_eq!(
        observed_checksum, expected_checksum,
        "checksum from component framework must match the packaged one"
    );
    decode_persistent(&config_bytes[fidl_start..])
        .expect("must be able to parse inner config as fidl")
}
