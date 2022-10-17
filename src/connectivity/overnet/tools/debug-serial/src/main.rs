// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Component that allows access to the kernel's debug serial line

use anyhow::{Context as _, Error};
use fidl_fuchsia_hardware_serial::{
    Class, DeviceProxy_Request, DeviceProxy_RequestStream, DeviceRequest,
};
use fidl_fuchsia_kernel::DebugResourceMarker;
use fuchsia_async::{unblock, Time, Timer};
use fuchsia_component::server::ServiceFs;
use fuchsia_zircon::{self as zx, DurationNum};
use futures::channel::mpsc;
use futures::lock::Mutex;
use futures::prelude::*;
use zx::sys::{zx_debug_read, zx_debug_write};
use zx::AsHandleRef;

enum IncomingService {
    DeviceProxy(DeviceProxy_RequestStream),
}

async fn writer_task(mut rx: mpsc::Receiver<Vec<u8>>) -> Result<(), Error> {
    while let Some(data) = rx.next().await {
        tracing::trace!("write bytes: {:?}", data);
        let data_len = data.len();
        unblock(move || {
            zx::Status::ok(unsafe { zx_debug_write(data.as_ptr(), data.len()) })
                .context("zx_debug_write failed: check your kernel command line; is kernel.enable-serial-syscalls=true, and kernel.serial=<<whatever is appropriate for your platform>>")?;
            Result::<(), Error>::Ok(())
        }).await?;
        Timer::new(Time::after((30 * data_len as i64).micros())).await;
    }
    Ok(())
}

async fn reader_task(
    mut tx: mpsc::Sender<Vec<u8>>,
    debug_resource: zx::Resource,
) -> Result<(), Error> {
    loop {
        let raw_handle = debug_resource.raw_handle();
        let v = unblock(move || {
            let mut buffer = [0u8; 1024];
            let mut actual = 0usize;
            zx::Status::ok(unsafe {
                zx_debug_read(raw_handle, buffer.as_mut_ptr(), buffer.len(), &mut actual)
            })
            .context("zx_debug_read")?;
            Ok::<Vec<u8>, Error>(buffer[..actual].to_vec())
        })
        .await?;
        tracing::trace!("got bytes: {:?}", &v);
        if let Err(e) = tx.send(v).await {
            tracing::warn!("failed to send read to channel: {:?}", e);
        }
    }
}

#[fuchsia::main(logging_tags = ["overnet_debug_proxy"])]
async fn main() -> Result<(), Error> {
    hoist::init_hoist()?;

    let mut fs = ServiceFs::new_local();
    let mut svc_dir = fs.dir("svc");
    svc_dir.add_fidl_service(IncomingService::DeviceProxy);
    fs.take_and_serve_directory_handle()?;

    let debug_resource =
        fuchsia_component::client::connect_to_protocol::<DebugResourceMarker>()?.get().await?;

    let (tx_write, rx_write) = mpsc::channel(0);
    let (tx_read, rx_read) = mpsc::channel(0);

    let reader = &Mutex::new(Some(rx_read));

    let r = future::try_join3(
        reader_task(tx_read, debug_resource),
        writer_task(rx_write),
        async move {
            fs.for_each_concurrent(None, move |IncomingService::DeviceProxy(requests)| {
                let tx_write = tx_write.clone();
                requests.for_each_concurrent(None, move |request| {
                    let tx_write = tx_write.clone();
                    async move {
                        match request {
                            Ok(request) => {
                                if let Some(mut r) = reader.lock().await.take() {
                                    run_safe(request, &mut tx_write.clone(), &mut r).await;
                                    *reader.lock().await = Some(r);
                                } else {
                                    tracing::warn!(
                                        "Failed to acquire debug resource (already taken)"
                                    )
                                }
                            }
                            Err(e) => tracing::warn!("Bad incoming request: {:?}", e),
                        }
                    }
                })
            })
            .await;
            Ok(())
        },
    )
    .map_ok(drop)
    .await;

    if let Err(e) = &r {
        tracing::error!("main loop failed: {:?}", e);
    }

    r
}

async fn run_safe(
    request: DeviceProxy_Request,
    write: &mut mpsc::Sender<Vec<u8>>,
    read: &mut mpsc::Receiver<Vec<u8>>,
) {
    let DeviceProxy_Request::GetChannel { req: request, control_handle: _ } = request;
    let mut request = match request.into_stream() {
        Ok(request) => request,
        Err(e) => {
            tracing::warn!("Failed to turn request into stream: {:?}", e);
            return;
        }
    };
    let r = run(&mut request, write, read).await;
    if let Err(e) = r {
        tracing::warn!("Request failed: {:?}", e);
    }
}

async fn run(
    requests: &mut (dyn Stream<Item = Result<DeviceRequest, fidl::Error>> + Unpin),
    write: &mut mpsc::Sender<Vec<u8>>,
    read: &mut mpsc::Receiver<Vec<u8>>,
) -> Result<(), Error> {
    let read = &Mutex::new(read);
    let write = &Mutex::new(write);
    requests
        .map_err(Into::into)
        .try_for_each_concurrent(None, |req| async move {
            tracing::trace!("handle request: {:?}", req);
            match req {
                DeviceRequest::Read { responder } => {
                    if let Some(read) = read.lock().await.next().await {
                        tracing::trace!("got read: {:?}", read);
                        responder.send(&mut Ok(read))?;
                    } else {
                        tracing::info!("no read (read thread done?)");
                    }
                }
                DeviceRequest::Write { data, responder } => {
                    const MAX_SEND_LENGTH: usize = 256;
                    let mut data: &[u8] = &data;
                    let mut write = write.lock().await;
                    while data.len() > MAX_SEND_LENGTH {
                        write.send(data[..MAX_SEND_LENGTH].to_vec()).await?;
                        data = &data[MAX_SEND_LENGTH..];
                    }
                    write.send(data.to_vec()).await?;
                    responder.send(&mut Ok(()))?;
                }
                DeviceRequest::GetClass { responder } => {
                    responder.send(Class::KernelDebug)?;
                }
                DeviceRequest::SetConfig { responder, .. } => {
                    responder.send(zx::Status::NOT_SUPPORTED.into_raw())?;
                }
            }
            Ok::<_, Error>(())
        })
        .await?;
    Ok(())
}

#[cfg(test)]
mod test {
    use super::*;
    use fidl_fuchsia_hardware_serial::{
        CharacterWidth, Config, DeviceMarker, DeviceProxy, FlowControl, Parity, StopWidth,
    };
    use fuchsia_async as fasync;

    struct TestProxy {
        proxy: DeviceProxy,
        writes: mpsc::Receiver<Vec<u8>>,
        reads: mpsc::Sender<Vec<u8>>,
    }

    fn test_proxy() -> Result<TestProxy, Error> {
        let (proxy, mut stream) = fidl::endpoints::create_proxy_and_stream::<DeviceMarker>()?;
        let (mut tx_wr, rx_wr) = mpsc::channel(1);
        let (tx_rd, mut rx_rd) = mpsc::channel(1);
        fasync::Task::local(async move { run(&mut stream, &mut tx_wr, &mut rx_rd).await.unwrap() })
            .detach();
        Ok(TestProxy { proxy, writes: rx_wr, reads: tx_rd })
    }

    #[fuchsia::test]
    async fn is_classy() -> Result<(), Error> {
        assert_eq!(test_proxy()?.proxy.get_class().await?, Class::KernelDebug);
        Ok(())
    }

    #[fuchsia::test]
    async fn config_not_supported() -> Result<(), Error> {
        assert_eq!(
            test_proxy()?
                .proxy
                .set_config(&mut Config {
                    character_width: CharacterWidth::Bits8,
                    stop_width: StopWidth::Bits1,
                    parity: Parity::Even,
                    control_flow: FlowControl::None,
                    baud_rate: 300,
                })
                .await?,
            zx::Status::NOT_SUPPORTED.into_raw()
        );
        Ok(())
    }

    #[fuchsia::test]
    async fn can_write() -> Result<(), Error> {
        let mut test_proxy = test_proxy()?;
        test_proxy.proxy.write(&[1, 2, 3]).await?.map_err(zx::Status::from_raw)?;
        assert_eq!(test_proxy.writes.next().await.unwrap(), vec![1, 2, 3]);
        Ok(())
    }

    #[fuchsia::test]
    async fn can_read() -> Result<(), Error> {
        let mut test_proxy = test_proxy()?;
        test_proxy.reads.send(vec![1, 2, 3]).await?;
        assert_eq!(test_proxy.proxy.read().await?.map_err(zx::Status::from_raw)?, vec![1, 2, 3]);
        Ok(())
    }
}
