// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    async_trait::async_trait,
    fidl_fuchsia_paver::{PayloadStreamRequest, PayloadStreamRequestStream, ReadInfo, ReadResult},
    fuchsia_zircon as zx,
    futures::lock::Mutex,
    futures::prelude::*,
    mapped_vmo::Mapping,
    remote_block_device::{BlockClient, MutableBufferSlice, RemoteBlockClient, VmoId},
    std::io::Read,
};

/// Callback type, called with (data_read, data_total)
pub trait StatusCallback: Send + Sync + Fn(usize, usize) -> () {}
impl<F> StatusCallback for F where F: Send + Sync + Fn(usize, usize) -> () {}

#[async_trait]
pub trait PayloadStreamer {
    /// Handle the server side of the PayloadStream service.
    async fn service_payload_stream_requests(
        self: Box<Self>,
        stream: PayloadStreamRequestStream,
    ) -> Result<(), Error>;

    /// Attach a callback that is called with status updates.
    async fn set_status_callback(&self, callback: Box<dyn StatusCallback>);
}

struct ReaderPayloadStreamerInner {
    src: Box<dyn Read + Sync + Send>,
    src_read: usize,           // Read offset into the reader.
    src_size: usize,           // Size of the reader.
    dest_buf: Option<Mapping>, // Maps the VMO used for the PayloadStream protocol.
    dest_size: usize,          // Size of the VMO used for the PayloadStream protocol.
    status_callback: Option<Box<dyn StatusCallback>>,
}

/// Streams the contents of a reader over the PayloadStream protocol.
pub struct ReaderPayloadStreamer {
    // We wrap all our state inside a mutex, to make it mutable.
    inner: Mutex<ReaderPayloadStreamerInner>,
}

impl ReaderPayloadStreamer {
    pub fn new(src: Box<dyn Read + Sync + Send>, src_size: usize) -> Self {
        ReaderPayloadStreamer {
            inner: Mutex::new(ReaderPayloadStreamerInner {
                src,
                src_read: 0,
                src_size,
                dest_buf: None,
                dest_size: 0,
                status_callback: None,
            }),
        }
    }

    /// Handle a single request from a FIDL client.
    async fn handle_request(self: &Self, req: PayloadStreamRequest) -> Result<(), Error> {
        let mut unwrapped = self.inner.lock().await;
        match req {
            PayloadStreamRequest::RegisterVmo { vmo, responder } => {
                // Make sure we only get bound once.
                if unwrapped.dest_buf.is_some() {
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

                let mapping = Mapping::create_from_vmo(
                    &vmo,
                    size,
                    zx::VmarFlags::PERM_READ | zx::VmarFlags::PERM_WRITE,
                );

                if let Err(e) = mapping {
                    responder.send(e.into_raw())?;
                    return Ok(());
                }

                unwrapped.dest_buf = Some(mapping.unwrap());
                unwrapped.dest_size = size;
                responder.send(zx::sys::ZX_OK)?;
            }
            PayloadStreamRequest::ReadData { responder } => {
                if unwrapped.dest_buf.is_none() || unwrapped.dest_size == 0 {
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

                unwrapped.dest_buf.as_ref().unwrap().write(&buf);

                unwrapped.src_read += read;
                responder.send(&mut ReadResult::Info {
                    0: ReadInfo { offset: 0, size: data_to_read as u64 },
                })?;

                let src_read = unwrapped.src_read;
                let src_size = unwrapped.src_size;
                if let Some(ref cb) = unwrapped.status_callback {
                    cb(src_read, src_size);
                }
            }
        }
        return Ok(());
    }
}

#[async_trait]
impl PayloadStreamer for ReaderPayloadStreamer {
    async fn service_payload_stream_requests(
        self: Box<Self>,
        stream: PayloadStreamRequestStream,
    ) -> Result<(), Error> {
        stream
            .map(|result| result.context("failed request"))
            .try_for_each(|request| async { self.handle_request(request).await })
            .await
    }

    async fn set_status_callback(&self, callback: Box<dyn StatusCallback>) {
        self.inner.lock().await.status_callback = Some(callback);
    }
}

struct BlockDevicePayloadStreamerInner {
    device: RemoteBlockClient,
    device_read: usize,        // Read offset into the block device.
    device_size: usize,        // Size of the block device.
    device_vmo_id: VmoId,      // VMO id used to read from the RemoteBlockClient.
    device_buf: Mapping, // Maps the VMO the RemoteBlockClient uses to read from the block device.
    device_vmo_read: usize, // Read offset into the VMO used to read from the block device.
    dest_buf: Option<Mapping>, // Maps the VMO used for the PayloadStream protocol.
    dest_size: usize,    // Size of the VMO used for the PayloadStream protocol.
    status_callback: Option<Box<dyn StatusCallback>>,
}

/// Streams the contents of a block device over the PayloadStream protocol.
pub struct BlockDevicePayloadStreamer {
    // We wrap all our state inside a mutex, to make it mutable.
    inner: Mutex<BlockDevicePayloadStreamerInner>,
}

//TODO(fxb/107831): Increasing this may speed up the transfer once the UMS crash is fixed.
const DEVICE_VMO_SIZE: usize = 8192 * 16;

impl BlockDevicePayloadStreamer {
    pub async fn new(block_device_path: &str) -> Result<Self, Error> {
        let (local, remote) = zx::Channel::create()?;
        fdio::service_connect(block_device_path, remote)?;
        let client = RemoteBlockClient::new(local).await?;

        let device_vmo = zx::Vmo::create(DEVICE_VMO_SIZE as u64)?;
        let device_vmo_id = client.attach_vmo(&device_vmo).await?;
        let device_buf = Mapping::create_from_vmo(
            &device_vmo,
            DEVICE_VMO_SIZE,
            zx::VmarFlags::PERM_READ | zx::VmarFlags::PERM_WRITE,
        )?;

        Ok(BlockDevicePayloadStreamer {
            inner: Mutex::new(BlockDevicePayloadStreamerInner {
                device_size: client.block_size() as usize * client.block_count() as usize,
                device: client,
                device_read: 0,
                device_vmo_id,
                device_buf,
                device_vmo_read: 0,
                dest_buf: None,
                dest_size: 0,
                status_callback: None,
            }),
        })
    }

    /// Handle a single request from a FIDL client.
    async fn handle_request(&self, req: PayloadStreamRequest) -> Result<(), Error> {
        let mut unwrapped = self.inner.lock().await;
        match req {
            PayloadStreamRequest::RegisterVmo { vmo, responder } => {
                // Make sure we only get bound once.
                if unwrapped.dest_buf.is_some() {
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
                // Simplified logic if the size of the VMO used to read from the device is some
                // multiple of the size of the VMO used for the PayloadStream protocol.
                assert_eq!(DEVICE_VMO_SIZE % size, 0);

                let mapping = Mapping::create_from_vmo(
                    &vmo,
                    size,
                    zx::VmarFlags::PERM_READ | zx::VmarFlags::PERM_WRITE,
                );

                if let Err(e) = mapping {
                    responder.send(e.into_raw())?;
                    return Ok(());
                }

                unwrapped.dest_buf = Some(mapping.unwrap());
                unwrapped.dest_size = size;

                responder.send(zx::sys::ZX_OK)?;
            }
            PayloadStreamRequest::ReadData { responder } => {
                if unwrapped.dest_buf.is_none() || unwrapped.dest_size == 0 {
                    responder.send(&mut ReadResult::Err { 0: zx::sys::ZX_ERR_BAD_STATE })?;
                    return Ok(());
                }

                let data_left = unwrapped.device_size - unwrapped.device_read;

                if data_left == 0 {
                    responder.send(&mut ReadResult::Eof { 0: true })?;
                    return Ok(());
                }

                // Check if we need to read more data from the block device.
                // We read more than `dest_size` bytes from the block device at a time for better
                // throughput.
                if unwrapped.device_read == 0 || unwrapped.device_vmo_read == DEVICE_VMO_SIZE {
                    let data_to_read = std::cmp::min(data_left, DEVICE_VMO_SIZE);
                    let buffer_slice = MutableBufferSlice::new_with_vmo_id(
                        &unwrapped.device_vmo_id,
                        0,
                        data_to_read as u64,
                    );

                    if let Err(e) =
                        unwrapped.device.read_at(buffer_slice, unwrapped.device_read as u64).await
                    {
                        responder.send(&mut ReadResult::Err {
                            0: e.downcast::<zx::Status>()?.into_raw(),
                        })?;
                        return Ok(());
                    }
                    unwrapped.device_vmo_read = 0;
                }

                let data_to_return = std::cmp::min(data_left, unwrapped.dest_size);

                // Copy data from the device VMO to the PayloadStream VMO.
                // Avoiding the double copy here doesn't speed up the stream significantly.
                let mut buf: Vec<u8> = vec![0; data_to_return];
                unwrapped.device_buf.read_at(unwrapped.device_vmo_read, &mut buf);
                unwrapped.dest_buf.as_ref().unwrap().write(&buf);

                unwrapped.device_vmo_read += data_to_return;
                unwrapped.device_read += data_to_return;

                responder.send(&mut ReadResult::Info {
                    0: ReadInfo { offset: 0, size: data_to_return as u64 },
                })?;

                let device_read = unwrapped.device_read;
                let device_size = unwrapped.device_size;
                if let Some(ref cb) = unwrapped.status_callback {
                    cb(device_read, device_size);
                }
            }
        }
        return Ok(());
    }

    async fn close(&self) -> Result<(), Error> {
        let unwrapped = self.inner.lock().await;
        unwrapped.device.detach_vmo(unwrapped.device_vmo_id.take()).await?;
        unwrapped.device.close().await
    }
}

#[async_trait]
impl PayloadStreamer for BlockDevicePayloadStreamer {
    async fn set_status_callback(&self, callback: Box<dyn StatusCallback>) {
        self.inner.lock().await.status_callback = Some(callback);
    }

    async fn service_payload_stream_requests(
        self: Box<Self>,
        stream: PayloadStreamRequestStream,
    ) -> Result<(), Error> {
        let result = stream
            .map(|result| result.context("failed request"))
            .try_for_each(|request| async { self.handle_request(request).await })
            .await;

        if let Err(e) = result {
            // Still attempt to close the client but ignore any errors.
            self.close().await.ok();
            return Err(e);
        }

        self.close().await
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
        futures::future::try_join,
        ramdevice_client::{RamdiskClient, VmoRamdiskClientBuilder},
        std::sync::Mutex,
        std::{io::Cursor, sync::Arc},
    };

    struct StatusUpdate {
        data_read: usize,
        data_size: usize,
    }

    async fn serve_payload(
        streamer: Box<dyn PayloadStreamer>,
    ) -> Result<
        (PayloadStreamProxy, Arc<Mutex<StatusUpdate>>, impl Future<Output = Result<(), Error>>),
        Error,
    > {
        let status = Arc::new(Mutex::new(StatusUpdate { data_read: 0, data_size: 0 }));
        let status_clone = Arc::clone(&status);
        let callback = move |data_read, data_size| {
            let mut val = status_clone.lock().unwrap();
            val.data_read = data_read;
            val.data_size = data_size;
        };
        streamer.set_status_callback(Box::new(callback)).await;

        let (client_end, server_end) = fidl::endpoints::create_endpoints::<PayloadStreamMarker>()?;
        let stream = server_end.into_stream()?;

        // Do not await as we return this Future so that the caller can run the client and server
        // concurrently.
        let server = streamer.service_payload_stream_requests(stream);

        return Ok((client_end.into_proxy()?, status, server));
    }

    fn create_ramdisk(src: Vec<u8>) -> Result<RamdiskClient, Error> {
        ramdevice_client::wait_for_device(
            "/dev/sys/platform/00:00:2d/ramctl",
            std::time::Duration::from_secs(10),
        )
        .expect("ramctl did not appear");
        let vmo = zx::Vmo::create(src.len() as u64).context("failed to create vmo")?;
        vmo.write(&src, 0).context("failed to write vmo")?;
        VmoRamdiskClientBuilder::new(vmo).build().context("failed to create ramdisk client")
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

    async fn run_client(
        proxy: PayloadStreamProxy,
        src_size: usize,
        dst_size: usize,
        byte: u8,
        callback_status: Arc<Mutex<StatusUpdate>>,
    ) -> Result<(), Error> {
        let buf: Vec<u8> = vec![byte; src_size];
        let vmo = attach_vmo(dst_size, &proxy).await?.1.expect("No vmo");
        let mut read = 0;
        while read < buf.len() {
            read = read_slice(&vmo, dst_size, &proxy, byte, read).await?;
            let data = callback_status.lock().unwrap();
            assert_eq!(data.data_size, src_size);
            assert_eq!(data.data_read, read);
        }

        expect_eof(&proxy).await
    }

    async fn do_one_test(
        src_size: usize,
        dst_size: usize,
        byte: u8,
        use_block_device_streamer: bool,
    ) -> Result<(), Error> {
        let buf: Vec<u8> = vec![byte; src_size];

        // Extend the ramdisk client's scope.
        let ramdisk_client: RamdiskClient;

        let streamer: Box<dyn PayloadStreamer> = if use_block_device_streamer {
            ramdisk_client = create_ramdisk(buf)?;
            Box::new(BlockDevicePayloadStreamer::new(ramdisk_client.get_path()).await?)
        } else {
            Box::new(ReaderPayloadStreamer::new(Box::new(Cursor::new(buf)), src_size))
        };

        let (proxy, callback_status, server) =
            serve_payload(streamer).await.context("serve payload failed")?;
        try_join(server, run_client(proxy, src_size, dst_size, byte, callback_status)).await?;

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_stream_simple() -> Result<(), Error> {
        do_one_test(200, 200, 0xaa, false).await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_large_src_buffer() -> Result<(), Error> {
        do_one_test(4096 * 10, 4096, 0x76, false).await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_large_dst_buffer() -> Result<(), Error> {
        do_one_test(4096, 4096 * 10, 0x76, false).await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_large_buffers() -> Result<(), Error> {
        do_one_test(4096 * 100, 4096 * 100, 0xfa, false).await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_multiple_registers() -> Result<(), Error> {
        let src_size = 4096 * 10;
        let dst_size = 4096;
        let byte: u8 = 0xab;
        let buf: Vec<u8> = vec![byte; src_size];
        let streamer: Box<dyn PayloadStreamer> =
            Box::new(ReaderPayloadStreamer::new(Box::new(Cursor::new(buf)), src_size));
        let (proxy, _, server) = serve_payload(streamer).await?;

        try_join(
            async move {
                let (_, vmo) = attach_vmo(dst_size, &proxy).await?;
                assert!(vmo.is_some());
                let (err, _) = attach_vmo(dst_size, &proxy).await?;
                assert_eq!(err, zx::sys::ZX_ERR_ALREADY_BOUND);
                Ok(())
            },
            server,
        )
        .await?;

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_block_streamer_simple() -> Result<(), Error> {
        do_one_test(4096, 8192, 0xaa, true).await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_block_streamer_large_src_buffer() -> Result<(), Error> {
        do_one_test(4096 * 100, 8192, 0x76, true).await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_block_streamer_large_dst_buffer() -> Result<(), Error> {
        do_one_test(4096, 8192 * 16, 0x76, true).await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_block_streamer_multiple_registers() -> Result<(), Error> {
        let src_size = 8192 * 10;
        let dst_size = 8192;
        let byte: u8 = 0xab;
        let buf: Vec<u8> = vec![byte; src_size];
        let ramdisk_client = create_ramdisk(buf)?;
        let streamer: Box<dyn PayloadStreamer> =
            Box::new(BlockDevicePayloadStreamer::new(ramdisk_client.get_path()).await?);
        let (proxy, _, server) = serve_payload(streamer).await?;

        try_join(
            async move {
                let (_, vmo) = attach_vmo(dst_size, &proxy).await?;
                assert!(vmo.is_some());
                let (err, _) = attach_vmo(dst_size, &proxy).await?;
                assert_eq!(err, zx::sys::ZX_ERR_ALREADY_BOUND);
                Ok(())
            },
            server,
        )
        .await?;

        Ok(())
    }
}
