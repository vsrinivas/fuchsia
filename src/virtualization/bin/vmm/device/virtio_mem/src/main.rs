// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod mem_device;
mod wire;

use {
    crate::mem_device::{MemDevice, VmoMemoryBackend},
    anyhow::{anyhow, Context},
    fidl::endpoints::RequestStream,
    fidl_fuchsia_virtualization_hardware::VirtioMemRequestStream,
    fuchsia_component::server,
    fuchsia_inspect as inspect,
    futures::{StreamExt, TryFutureExt, TryStreamExt},
    tracing,
    virtio_device::chain::ReadableChain,
};

async fn run_virtio_mem(mut virtio_mem_fidl: VirtioMemRequestStream) -> Result<(), anyhow::Error> {
    // Receive start info as first message.
    let (start_info, plugged_block_size, plugged_region_size, responder) = virtio_mem_fidl
        .try_next()
        .await?
        .ok_or(anyhow!("Failed to read fidl message from the channel."))?
        .into_start()
        .ok_or(anyhow!("Start should be the first message sent."))?;

    // Prepare the device builder
    let (mut device_builder, guest_mem) = machina_virtio_device::from_start_info(start_info)?;
    responder.send()?;
    let vmo = device_builder.take_vmo().expect("VMO must be provided to virtio_mem device");

    // Complete the setup of queues and get a device.
    let mut virtio_device_fidl = virtio_mem_fidl.cast_stream();
    let (device, ready_responder) = machina_virtio_device::config_builder_from_stream(
        device_builder,
        &mut virtio_device_fidl,
        &[wire::GUESTREQUESTQ][..],
        &guest_mem,
    )
    .await
    .context("Failed to initialize device.")?;

    // Initialize all queues.
    let guest_request_stream = device.take_stream(wire::GUESTREQUESTQ)?;

    ready_responder.send()?;
    let mem_device = MemDevice::new(
        VmoMemoryBackend::new(vmo),
        &inspect::component::inspector().root(),
        plugged_block_size,
        plugged_region_size,
    );

    futures::future::try_join(
        device.run_device_notify(virtio_device_fidl).err_into(),
        guest_request_stream.map(|chain| Ok(chain)).try_for_each(|chain| {
            futures::future::ready({
                mem_device.process_guest_request_chain(ReadableChain::new(chain, &guest_mem));
                Ok(())
            })
        }),
    )
    .await
    .map(|((), ())| ())
}

#[fuchsia::main(logging = true, threads = 1)]
async fn main() -> Result<(), anyhow::Error> {
    let mut fs = server::ServiceFs::new();
    inspect_runtime::serve(inspect::component::inspector(), &mut fs)?;
    fs.dir("svc").add_fidl_service(|stream: VirtioMemRequestStream| stream);
    fs.take_and_serve_directory_handle().context("Error starting server")?;
    fs.for_each_concurrent(None, |stream| async {
        if let Err(e) = run_virtio_mem(stream).await {
            tracing::error!("Error running virtio_mem service: {}", e);
        }
    })
    .await;
    Ok(())
}
