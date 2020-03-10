// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_paver::{PayloadStreamRequest, ReadInfo, ReadResult},
    fuchsia_zircon as zx,
    std::io::Read,
    std::sync::Mutex,
};

struct PayloadStreamerInner {
    src: Box<dyn Read + Sync + Send>,
    src_read: usize,
    src_size: usize,
    dest_vmo: Option<fidl::Vmo>,
    dest_size: usize,
}

/// A simple VMO-backed implementation of the
/// PayloadStream protocol.
pub struct PayloadStreamer {
    // We wrap all our state inside a mutex, to make it mutable.
    inner: Mutex<PayloadStreamerInner>,
}

impl PayloadStreamer {
    pub fn new(src: Box<dyn Read + Sync + Send>, src_size: usize) -> Self {
        PayloadStreamer {
            inner: Mutex::new(PayloadStreamerInner {
                src,
                src_read: 0,
                src_size,
                dest_vmo: None,
                dest_size: 0,
            }),
        }
    }

    /// Handle a single request from a FIDL client.
    pub async fn handle_request(self: &Self, req: PayloadStreamRequest) -> Result<(), Error> {
        let mut unwrapped = self.inner.lock().unwrap();
        match req {
            PayloadStreamRequest::RegisterVmo { vmo, responder } => {
                // Make sure we only get bound once.
                if unwrapped.dest_vmo.is_some() {
                    responder.send(zx::sys::ZX_ERR_ALREADY_BOUND)?;
                    return Ok(());
                }

                // Figure out information about the new VMO.
                let size = vmo.get_size();
                if let Err(e) = size {
                    responder.send(e.into_raw())?;
                    return Ok(());
                }

                let size = size.unwrap() as usize;
                unwrapped.dest_vmo = Some(vmo);
                unwrapped.dest_size = size;
                responder.send(zx::sys::ZX_OK)?;
            }
            PayloadStreamRequest::ReadData { responder } => {
                if unwrapped.dest_vmo == None || unwrapped.dest_size == 0 {
                    responder.send(&mut ReadResult::Err { 0: zx::sys::ZX_ERR_BAD_STATE })?;
                    return Ok(());
                }

                let data_left = unwrapped.src_size - unwrapped.src_read;
                let data_to_read = std::cmp::min(data_left, unwrapped.dest_size);
                let mut buf: Vec<u8> = vec![0; data_to_read];
                let read = unwrapped.src.read(&mut buf);
                if let Err(e) = read {
                    responder.send(&mut ReadResult::Err {
                        0: e.raw_os_error().unwrap_or(zx::sys::ZX_ERR_INTERNAL),
                    })?;
                    return Ok(());
                }
                let read = read?;
                if read == 0 {
                    responder.send(&mut ReadResult::Eof { 0: true })?;
                    return Ok(());
                }

                let write_result = unwrapped.dest_vmo.as_ref().unwrap().write(&buf, 0);
                if let Err(e) = write_result {
                    responder.send(&mut ReadResult::Err { 0: e.into_raw() })?;
                } else {
                    unwrapped.src_read += read;
                    responder.send(&mut ReadResult::Info {
                        0: ReadInfo { offset: 0, size: data_to_read as u64 },
                    })?;
                }
            }
        }
        return Ok(());
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::Context,
        fidl_fuchsia_paver::{PayloadStreamMarker, PayloadStreamProxy},
        fuchsia_async as fasync,
        fuchsia_zircon::{self as zx, HandleBased},
        futures::prelude::*,
        std::io::Cursor,
    };

    fn serve_payload(src: Vec<u8>) -> Result<PayloadStreamProxy, Error> {
        let size = src.len();
        let streamer = PayloadStreamer::new(Box::new(Cursor::new(src)), size);
        let (client_end, server_end) = fidl::endpoints::create_endpoints::<PayloadStreamMarker>()?;
        let mut stream = server_end.into_stream()?;

        fasync::spawn(async move {
            while let Some(req) = stream.try_next().await.expect("Failed to get request!") {
                streamer.handle_request(req).await.expect("Failed to handle request!");
            }
        });

        return Ok(client_end.into_proxy()?);
    }

    fn setup_proxy(src_size: usize, byte: u8) -> Result<PayloadStreamProxy, Error> {
        let buf: Vec<u8> = vec![byte; src_size];
        let proxy = serve_payload(buf).context("serve payload failed")?;
        Ok(proxy)
    }

    async fn attach_vmo(
        vmo_size: usize,
        proxy: &PayloadStreamProxy,
    ) -> Result<(i32, Option<zx::Vmo>), anyhow::Error> {
        let local_vmo = zx::Vmo::create(vmo_size as u64)?;
        let registered_vmo = local_vmo.duplicate_handle(zx::Rights::SAME_RIGHTS)?;
        let ret = proxy.register_vmo(registered_vmo).await?;
        if ret != zx::Status::OK.into_raw() {
            Ok((ret, None))
        } else {
            Ok((zx::Status::OK.into_raw(), Some(local_vmo)))
        }
    }

    async fn read_slice(
        vmo: &zx::Vmo,
        vmo_size: usize,
        proxy: &PayloadStreamProxy,
        byte: u8,
        mut read: usize,
    ) -> Result<usize, Error> {
        let ret = proxy.read_data().await?;
        match ret {
            ReadResult::Err { 0: err } => {
                panic!("read_data failed: {}", err);
            }
            ReadResult::Eof { 0: boolean } => {
                panic!("unexpected eof: {}", boolean);
            }

            ReadResult::Info { 0: info } => {
                let mut written_buf: Vec<u8> = vec![0; vmo_size];
                let slice = &mut written_buf[0..info.size as usize];
                vmo.read(slice, info.offset)?;
                for (i, val) in slice.iter().enumerate() {
                    assert_eq!(*val, byte, "byte {} was wrong", i + read);
                }
                read += info.size as usize;
            }
        }

        Ok(read)
    }

    async fn expect_eof(proxy: &PayloadStreamProxy) -> Result<(), Error> {
        let ret = proxy.read_data().await?;
        if let ReadResult::Eof { 0: _ } = ret {
            return Ok(());
        } else {
            panic!("Should be at EOF but not at EOF!");
        }
    }

    async fn do_one_test(src_size: usize, dst_size: usize, byte: u8) -> Result<(), Error> {
        let buf: Vec<u8> = vec![byte; src_size];
        let proxy = setup_proxy(src_size, byte)?;
        let vmo = attach_vmo(dst_size, &proxy).await?.1.expect("No vmo");
        let mut read = 0;
        while read < buf.len() {
            read = read_slice(&vmo, dst_size, &proxy, byte, read).await?;
        }

        expect_eof(&proxy).await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_stream_simple() -> Result<(), Error> {
        do_one_test(200, 200, 0xaa).await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_large_src_buffer() -> Result<(), Error> {
        do_one_test(4096 * 10, 4096, 0x76).await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_large_dst_buffer() -> Result<(), Error> {
        do_one_test(4096, 4096 * 10, 0x76).await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_large_buffers() -> Result<(), Error> {
        do_one_test(4096 * 100, 4096 * 100, 0xfa).await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_multiple_registers() -> Result<(), Error> {
        let src_size = 4096 * 10;
        let dst_size = 4096;
        let byte: u8 = 0xab;
        let proxy = setup_proxy(src_size, byte)?;
        let (_, vmo) = attach_vmo(dst_size, &proxy).await?;
        assert!(vmo.is_some());
        let (err, _) = attach_vmo(dst_size, &proxy).await?;
        assert_eq!(err, zx::sys::ZX_ERR_ALREADY_BOUND);

        Ok(())
    }
}
