// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context as _, Error};
use fidl::endpoints::{create_proxy, ServiceMarker};
use fidl_fuchsia_hardware_serial::{
    NewDeviceProxy, NewDeviceProxy_Marker, NewDeviceReadResult, NewDeviceWriteResult,
};
use fuchsia_zircon as zx;
use futures::prelude::*;
use overnet_core::Router;
use serial_link::{
    descriptor::Descriptor,
    report_skipped::ReportSkipped,
    run::{run, Role},
};
use std::pin::Pin;
use std::sync::Weak;
use std::task::{Context, Poll};

pub async fn run_serial_link_handlers(
    router: Weak<Router>,
    descriptors: String,
) -> Result<(), Error> {
    eprintln!("SERIAL DESCRIPTORS: {}", descriptors);
    futures::stream::iter(serial_link::descriptor::parse(&descriptors).await?.into_iter())
        .map(Ok)
        .try_for_each_concurrent(None, |desc| {
            let router = router.clone();
            async move {
                let (cli, svr) = create_proxy()?;
                let text_desc = format!("{:?}", desc);
                match desc {
                    Descriptor::Debug => {
                        fuchsia_component::client::connect_to_service::<NewDeviceProxy_Marker>()?
                            .get_channel(svr)
                            .context(format!(
                                "connecting to service {}",
                                NewDeviceProxy_Marker::NAME
                            ))?;
                    }
                    Descriptor::Device { ref path, mut config } => {
                        fdio::service_connect(
                            path.to_str()
                                .ok_or_else(|| format_err!("path not utf8 encoded: {:?}", path))?,
                            svr.into_channel(),
                        )
                        .with_context(|| format!("Error connecting to service path: {:?}", path))?;
                        zx::Status::ok(cli.set_config(&mut config).await?)?;
                    }
                }
                let (rx, tx) = Dev::new(cli).split();
                let error = run(
                    Role::Server,
                    rx,
                    tx,
                    router,
                    ReportSkipped::new("skipped serial bytes"),
                    Some(&desc),
                )
                .await;
                eprintln!("SERIAL LINK {} completed with failure: {:?}", text_desc, error);
                Ok(())
            }
        })
        .await
}

type PendingRead = fidl::client::QueryResponseFut<NewDeviceReadResult>;
type PendingWrite = fidl::client::QueryResponseFut<NewDeviceWriteResult>;
type IOResult = Result<usize, std::io::Error>;

enum ReadState {
    Idle,
    Pending(PendingRead),
    Buffered(Vec<u8>),
}

enum WriteState {
    Idle,
    Pending(PendingWrite),
}

fn convert_io_result<R>(
    r: Result<Result<R, zx::sys::zx_status_t>, fidl::Error>,
) -> Result<R, std::io::Error> {
    match r {
        Ok(Ok(r)) => Ok(r),
        Err(e) => {
            log::trace!("serial i/o fidl error: {:?}", e);
            Err(std::io::Error::new(std::io::ErrorKind::Other, e))
        }
        Ok(Err(zx::sys::ZX_OK)) => panic!(),
        Ok(Err(e)) => {
            log::trace!("serial i/o zircon error: {:?}", e);
            Err(zx::Status::from_raw(e).into_io_error())
        }
    }
}

struct Dev {
    proxy: NewDeviceProxy,
    read_state: ReadState,
    write_state: WriteState,
}

impl Dev {
    fn new(proxy: NewDeviceProxy) -> Dev {
        Dev { proxy, read_state: ReadState::Idle, write_state: WriteState::Idle }
    }

    fn continue_pending_read(
        mut self: Pin<&mut Self>,
        ctx: &mut Context<'_>,
        bytes: &mut [u8],
        mut read: PendingRead,
    ) -> Poll<IOResult> {
        match read.poll_unpin(ctx) {
            Poll::Pending => {
                self.read_state = ReadState::Pending(read);
                Poll::Pending
            }
            Poll::Ready(r) => self.continue_buffered_read(bytes, convert_io_result(r)?),
        }
    }

    fn continue_buffered_read(
        mut self: Pin<&mut Self>,
        bytes: &mut [u8],
        mut buffer: Vec<u8>,
    ) -> Poll<IOResult> {
        let bytes_len = bytes.len();
        let buffer_len = buffer.len();
        if bytes_len == buffer_len {
            bytes.copy_from_slice(&buffer);
            Poll::Ready(Ok(bytes_len))
        } else if bytes_len < buffer_len {
            bytes.iter_mut().zip(buffer.drain(..bytes_len)).for_each(|(dst, src)| *dst = src);
            self.read_state = ReadState::Buffered(buffer);
            Poll::Ready(Ok(bytes_len))
        } else {
            // bytes_len > buffer_len
            bytes[..buffer_len].copy_from_slice(&buffer);
            Poll::Ready(Ok(buffer_len))
        }
    }
}

impl AsyncRead for Dev {
    fn poll_read(
        mut self: Pin<&mut Self>,
        ctx: &mut Context<'_>,
        bytes: &mut [u8],
    ) -> Poll<IOResult> {
        match std::mem::replace(&mut self.read_state, ReadState::Idle) {
            ReadState::Idle => {
                let read = self.proxy.read();
                self.continue_pending_read(ctx, bytes, read)
            }
            ReadState::Pending(read) => self.continue_pending_read(ctx, bytes, read),
            ReadState::Buffered(buffer) => self.continue_buffered_read(bytes, buffer),
        }
    }
}

impl AsyncWrite for Dev {
    fn poll_write(mut self: Pin<&mut Self>, ctx: &mut Context<'_>, bytes: &[u8]) -> Poll<IOResult> {
        let mut write = match std::mem::replace(&mut self.write_state, WriteState::Idle) {
            WriteState::Idle => self.proxy.write(bytes),
            WriteState::Pending(write) => write,
        };
        match write.poll_unpin(ctx) {
            Poll::Pending => {
                self.write_state = WriteState::Pending(write);
                Poll::Pending
            }
            Poll::Ready(r) => {
                convert_io_result(r)?;
                Poll::Ready(Ok(bytes.len()))
            }
        }
    }

    fn poll_flush(
        self: Pin<&mut Self>,
        _ctx: &mut Context<'_>,
    ) -> Poll<Result<(), std::io::Error>> {
        Poll::Ready(Ok(()))
    }

    fn poll_close(
        self: Pin<&mut Self>,
        _ctx: &mut Context<'_>,
    ) -> Poll<Result<(), std::io::Error>> {
        unimplemented!();
    }
}
