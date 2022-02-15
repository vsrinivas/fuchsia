// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_test_structuredconfig_receiver::{
    ConfigReceiverPuppetRequest, ConfigReceiverPuppetRequestStream, ReceiverConfig,
};
use fuchsia_component::server::ServiceFs;
use futures::StreamExt;
use receiver_config::Config;

enum IncomingRequest {
    Puppet(ConfigReceiverPuppetRequestStream),
}

#[fuchsia::component]
async fn main() {
    let mut fs = ServiceFs::new_local();
    let inspector = fuchsia_inspect::component::inspector();
    inspect_runtime::serve(inspector, &mut fs).unwrap();

    let config = Config::from_args().record_to_inspect(inspector.root());
    let receiver_config = generated_to_puppet_defined(config);

    fs.dir("svc").add_fidl_service(IncomingRequest::Puppet);
    fs.take_and_serve_directory_handle().unwrap();
    fs.for_each_concurrent(None, move |request: IncomingRequest| {
        let mut receiver_config = receiver_config.clone();
        async move {
            match request {
                IncomingRequest::Puppet(mut reqs) => {
                    while let Some(Ok(req)) = reqs.next().await {
                        match req {
                            ConfigReceiverPuppetRequest::GetConfig { responder } => {
                                responder.send(&mut receiver_config).unwrap()
                            }
                        }
                    }
                }
            }
        }
    })
    .await;
}

fn generated_to_puppet_defined(input: Config) -> ReceiverConfig {
    ReceiverConfig {
        my_flag: input.my_flag,
        my_uint8: input.my_uint8,
        my_uint16: input.my_uint16,
        my_uint32: input.my_uint32,
        my_uint64: input.my_uint64,
        my_int8: input.my_int8,
        my_int16: input.my_int16,
        my_int32: input.my_int32,
        my_int64: input.my_int64,
        my_string: input.my_string,
        my_vector_of_flag: input.my_vector_of_flag,
        my_vector_of_uint8: input.my_vector_of_uint8,
        my_vector_of_uint16: input.my_vector_of_uint16,
        my_vector_of_uint32: input.my_vector_of_uint32,
        my_vector_of_uint64: input.my_vector_of_uint64,
        my_vector_of_int8: input.my_vector_of_int8,
        my_vector_of_int16: input.my_vector_of_int16,
        my_vector_of_int32: input.my_vector_of_int32,
        my_vector_of_int64: input.my_vector_of_int64,
        my_vector_of_string: input.my_vector_of_string,
    }
}
