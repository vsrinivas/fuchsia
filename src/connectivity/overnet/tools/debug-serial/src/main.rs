// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Component that allows access to the kernel's debug serial line

use anyhow::Error;
use fidl_fuchsia_boot::RootResourceMarker;
use fidl_fuchsia_hardware_serial::{
    Class, NewDeviceProxy_Request, NewDeviceProxy_RequestStream, NewDeviceRequest,
};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_zircon as zx;
use futures::channel::mpsc;
use futures::lock::Mutex;
use futures::prelude::*;
use zx::sys::{zx_debug_read, zx_debug_write};
use zx::AsHandleRef;

enum IncomingService {
    NewDeviceProxy(NewDeviceProxy_RequestStream),
}

fn write_thread(mut rx: mpsc::Receiver<Vec<u8>>) {
    fasync::Executor::new().unwrap().run_singlethreaded(async move {
        while let Some(data) = rx.next().await {
            if let Err(e) = zx::Status::ok(unsafe { zx_debug_write(data.as_ptr(), data.len()) }) {
                log::warn!("zx_debug_write failed: {:?}", e);
                return;
            }
            std::thread::sleep(std::time::Duration::from_micros(30 * data.len() as u64));
        }
    })
}

fn read_thread(mut tx: mpsc::Sender<Vec<u8>>, root_resource: zx::Resource) {
    fasync::Executor::new().unwrap().run_singlethreaded(async move {
        loop {
            let mut buffer = [0u8; 1024];
            let mut actual = 0usize;
            if let Err(e) = zx::Status::ok(unsafe {
                zx_debug_read(
                    root_resource.raw_handle(),
                    buffer.as_mut_ptr(),
                    buffer.len(),
                    &mut actual,
                )
            }) {
                log::warn!("zx_debug_read failed: {:?}", e);
                return;
            }
            if let Err(e) = tx.send((&buffer[..actual]).to_vec()).await {
                log::warn!("failed to send read to channel: {:?}", e);
            }
        }
    })
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["overnet_debug_proxy"])?;

    let mut fs = ServiceFs::new_local();
    let mut svc_dir = fs.dir("svc");
    svc_dir.add_fidl_service(IncomingService::NewDeviceProxy);
    fs.take_and_serve_directory_handle()?;

    let root_resource =
        fuchsia_component::client::connect_to_service::<RootResourceMarker>()?.get().await?;

    let (tx_write, rx_write) = mpsc::channel(0);
    let (tx_read, rx_read) = mpsc::channel(0);

    let read_thread = std::thread::spawn(move || read_thread(tx_read, root_resource));
    let write_thread = std::thread::spawn(move || write_thread(rx_write));

    let reader = &Mutex::new(Some(rx_read));

    fs.for_each_concurrent(None, move |requests| {
        let tx_write = tx_write.clone();
        async move {
            let IncomingService::NewDeviceProxy(requests) = requests;
            let r = requests
                .try_for_each_concurrent(None, move |request| {
                    let tx_write = tx_write.clone();
                    async move {
                        if let Some(mut r) = reader.lock().await.take() {
                            run_safe(request, &mut tx_write.clone(), &mut r).await;
                            *reader.lock().await = Some(r);
                        } else {
                            log::warn!("Failed to acquire root resource (already taken)")
                        }
                        Ok(())
                    }
                })
                .await;
            if let Err(e) = r {
                log::warn!("Request stream failed: {:?}", e);
            }
        }
    })
    .await;

    read_thread.join().unwrap();
    write_thread.join().unwrap();

    Ok(())
}

async fn run_safe(
    request: NewDeviceProxy_Request,
    write: &mut mpsc::Sender<Vec<u8>>,
    read: &mut mpsc::Receiver<Vec<u8>>,
) {
    let NewDeviceProxy_Request::GetChannel { req: request, control_handle: _ } = request;
    let mut request = match request.into_stream() {
        Ok(request) => request,
        Err(e) => {
            log::warn!("Failed to turn request into stream: {:?}", e);
            return;
        }
    };
    let r = run(&mut request, write, read).await;
    if let Err(e) = r {
        log::warn!("Request failed: {:?}", e);
    }
}

async fn run(
    requests: &mut (dyn Stream<Item = Result<NewDeviceRequest, fidl::Error>> + Unpin),
    write: &mut mpsc::Sender<Vec<u8>>,
    read: &mut mpsc::Receiver<Vec<u8>>,
) -> Result<(), Error> {
    let read = &Mutex::new(read);
    let write = &Mutex::new(write);
    requests
        .map_err(Into::into)
        .try_for_each_concurrent(None, |req| async move {
            log::info!("handle request: {:?}", req);
            match req {
                NewDeviceRequest::Read { responder } => {
                    if let Some(read) = read.lock().await.next().await {
                        log::info!("got read: {:?}", read);
                        responder.send(&mut Ok(read))?;
                    } else {
                        log::info!("no read (read thread done?)");
                    }
                }
                NewDeviceRequest::Write { data, responder } => {
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
                NewDeviceRequest::GetClass { responder } => {
                    responder.send(Class::KernelDebug)?;
                }
                NewDeviceRequest::SetConfig { responder, .. } => {
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
        CharacterWidth, Config, FlowControl, NewDeviceMarker, NewDeviceProxy, Parity, StopWidth,
    };

    struct TestProxy {
        proxy: NewDeviceProxy,
        writes: mpsc::Receiver<Vec<u8>>,
        reads: mpsc::Sender<Vec<u8>>,
    }

    fn test_proxy() -> Result<TestProxy, Error> {
        let (proxy, mut stream) = fidl::endpoints::create_proxy_and_stream::<NewDeviceMarker>()?;
        let (mut tx_wr, rx_wr) = mpsc::channel(1);
        let (tx_rd, mut rx_rd) = mpsc::channel(1);
        fasync::spawn_local(async move { run(&mut stream, &mut tx_wr, &mut rx_rd).await.unwrap() });
        Ok(TestProxy { proxy, writes: rx_wr, reads: tx_rd })
    }

    #[fasync::run_singlethreaded(test)]
    async fn is_classy() -> Result<(), Error> {
        assert_eq!(test_proxy()?.proxy.get_class().await?, Class::KernelDebug);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
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

    #[fasync::run_singlethreaded(test)]
    async fn can_write() -> Result<(), Error> {
        let mut test_proxy = test_proxy()?;
        test_proxy.proxy.write(&[1, 2, 3]).await?.map_err(zx::Status::from_raw)?;
        assert_eq!(test_proxy.writes.next().await.unwrap(), vec![1, 2, 3]);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn can_read() -> Result<(), Error> {
        let mut test_proxy = test_proxy()?;
        test_proxy.reads.send(vec![1, 2, 3]).await?;
        assert_eq!(test_proxy.proxy.read().await?.map_err(zx::Status::from_raw)?, vec![1, 2, 3]);
        Ok(())
    }
}
