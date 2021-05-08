// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error, fidl_fuchsia_input_injection::InputDeviceRegistryRequestStream,
    fuchsia_async as fasync, fuchsia_component::server::ServiceFs, fuchsia_syslog::fx_log_warn,
    futures::StreamExt, input_pipeline as input_pipeline_lib, input_pipeline::input_device,
};

mod input_handlers;

enum ExposedServices {
    InputDeviceRegistry(InputDeviceRegistryRequestStream),
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["input_pipeline"]).expect("Failed to init syslog");

    // Expose InputDeviceRegistry to allow input injection for testing.
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(ExposedServices::InputDeviceRegistry);
    fs.take_and_serve_directory_handle()?;

    // Declare the types of input to support.
    let device_types = vec![input_device::InputDeviceType::MediaButtons];

    // Create a new input pipeline.
    let input_pipeline = input_pipeline_lib::input_pipeline::InputPipeline::new(
        device_types.clone(),
        input_handlers::create(),
    )
    .await
    .expect("Failed to create input pipeline");

    let input_event_sender = input_pipeline.input_event_sender.clone();
    let bindings = input_pipeline.input_device_bindings.clone();

    // Handle input events.
    fasync::Task::local(input_pipeline.handle_input_events()).detach();

    // Handle incoming injection input devices.
    while let Some(service_request) = fs.next().await {
        match service_request {
            ExposedServices::InputDeviceRegistry(stream) => {
                let input_device_registry_fut = handle_input_device_registry_request_stream(
                    stream,
                    device_types.clone(),
                    input_event_sender.clone(),
                    bindings.clone(),
                );

                fasync::Task::local(input_device_registry_fut).detach();
            }
        }
    }

    Ok(())
}

async fn handle_input_device_registry_request_stream(
    stream: InputDeviceRegistryRequestStream,
    device_types: Vec<input_pipeline_lib::input_device::InputDeviceType>,
    input_event_sender: futures::channel::mpsc::Sender<
        input_pipeline_lib::input_device::InputEvent,
    >,
    bindings: input_pipeline_lib::input_pipeline::InputDeviceBindingHashMap,
) {
    match input_pipeline_lib::input_pipeline::InputPipeline::handle_input_device_registry_request_stream(
        stream,
        &device_types,
        &input_event_sender,
        &bindings,
    )
    .await
    {
        Ok(()) => (),
        Err(e) => {
            fx_log_warn!(
                "failure while serving InputDeviceRegistry: {}; \
                     will continue serving other clients",
                e
            );
        }
    }
}
