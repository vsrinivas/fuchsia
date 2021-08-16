// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context},
    fidl::endpoints::RequestStream,
    fidl_fuchsia_virtualization_hardware::VirtioRngRequestStream,
    fuchsia_async::{self as fasync},
    fuchsia_component::server,
    fuchsia_syslog::{self as syslog},
    fuchsia_zircon::{self as zx},
    futures::{StreamExt, TryFutureExt, TryStreamExt},
    machina_virtio_device::{config_builder_from_stream, from_start_info},
    virtio_device::chain::WritableChain,
};

async fn run_virtio_rng(mut con: VirtioRngRequestStream) -> Result<(), anyhow::Error> {
    // Receive start info as first message.
    let (start_info, responder) = con
        .try_next()
        .await?
        .ok_or(anyhow!("Unexpected end of stream"))?
        .into_start()
        .ok_or(anyhow!("Expected Start message"))?;

    // Prepare the device builder
    let mut con = con.cast_stream();
    let (device_builder, guest_mem) = from_start_info(start_info)?;
    // Start info was good, acknowledge the message.
    responder.send()?;
    // Complete the setup of queues and get a device.
    let (device, ready_responder) =
        config_builder_from_stream(device_builder, &mut con, &[0][..], &guest_mem).await?;

    let desc_stream = device.take_stream(0)?;

    // Confirm ready.
    ready_responder.send()?;

    // Helper that fills a descriptor chain with cprng
    let write_chain = |chain| -> Result<(), anyhow::Error> {
        let mut iter = WritableChain::new(chain, &guest_mem)?;
        while let Some(mem) = iter.next().transpose()? {
            unsafe {
                zx::sys::zx_cprng_draw(
                    mem.try_mut_ptr().ok_or(anyhow!("Invalid descriptor address"))?,
                    mem.len(),
                );
            }
            iter.add_written(mem.len() as u32);
        }
        Ok(())
    };

    // Process everything to completion.
    futures::future::try_join(
        device.run_device_notify(con).err_into(),
        desc_stream.map(write_chain).try_for_each(|_| futures::future::ready(Ok(()))),
    )
    .await
    .map(|((), ())| ())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), anyhow::Error> {
    syslog::init().context("Unable to initialize syslog")?;
    let mut fs = server::ServiceFs::new();
    fs.dir("svc").add_fidl_service(|stream: VirtioRngRequestStream| stream);
    fs.take_and_serve_directory_handle().context("Error starting server")?;

    fs.for_each_concurrent(None, |stream| async {
        if let Err(e) = run_virtio_rng(stream).await {
            syslog::fx_log_err!("Error {} running virtio_rng service", e);
        }
    })
    .await;
    Ok(())
}
