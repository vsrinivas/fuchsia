// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::{
        errors::FxfsError,
        server::{errors::map_to_status, file::FxFile, node::OpenedNode},
    },
    anyhow::{bail, Error},
    fidl_fuchsia_hardware_block::{self as block, BlockRequest},
    fuchsia_async::{self as fasync, FifoReadable, FifoWritable},
    fuchsia_zircon as zx,
    futures::{stream::TryStreamExt, try_join},
    remote_block_device::{BlockFifoRequest, BlockFifoResponse},
    std::{collections::BTreeMap, option::Option, sync::Mutex},
    vfs::file::File,
};

/// Implements server to handle Block requests
pub struct BlockServer {
    file: OpenedNode<FxFile>,
    server_channel: Option<zx::Channel>,
    vmos: Mutex<BTreeMap<u16, zx::Vmo>>,
}

impl BlockServer {
    /// Creates a new BlockServer given a server channel to listen on.
    pub fn new(server_channel: zx::Channel, file: OpenedNode<FxFile>) -> BlockServer {
        BlockServer {
            file,
            server_channel: Some(server_channel),
            vmos: Mutex::new(BTreeMap::new()),
        }
    }

    // Returns a VMO id that is currently not being used
    fn get_vmo_id(&self, vmo: zx::Vmo) -> Option<u16> {
        let mut vmos = self.vmos.lock().unwrap();
        let mut prev_id = 0;
        for &id in vmos.keys() {
            if id != prev_id + 1 {
                let vmo_id = prev_id + 1;
                vmos.insert(vmo_id, vmo);
                return Some(vmo_id);
            }
            prev_id = id;
        }
        if prev_id < std::u16::MAX {
            let vmo_id = prev_id + 1;
            vmos.insert(vmo_id, vmo);
            Some(vmo_id)
        } else {
            None
        }
    }

    async fn handle_request(
        &self,
        request: BlockRequest,
        maybe_server_fifo: &Mutex<Option<zx::Fifo>>,
    ) -> Result<(), Error> {
        match request {
            BlockRequest::GetInfo { responder } => {
                let mut block_info = block::BlockInfo {
                    block_count: 1024,
                    block_size: self.file.get_block_size() as u32,
                    max_transfer_size: 1024 * 1024,
                    flags: 0,
                    reserved: 0,
                };
                responder.send(zx::sys::ZX_OK, Some(&mut block_info))?;
            }
            BlockRequest::GetFifo { responder } => {
                responder.send(zx::sys::ZX_OK, maybe_server_fifo.lock().unwrap().take())?;
            }
            BlockRequest::AttachVmo { vmo, responder } => match self.get_vmo_id(vmo) {
                Some(vmo_id) => {
                    responder.send(zx::sys::ZX_OK, Some(&mut block::VmoId { id: vmo_id }))?
                }
                None => responder.send(zx::sys::ZX_ERR_NO_RESOURCES, None)?,
            },
            BlockRequest::CloseFifo { responder } => {
                // TODO(fxbug.dev/89873): close fifo
                responder.send(zx::sys::ZX_OK)?;
            }
            _ => bail!("Unexpected message"),
        }
        Ok(())
    }

    async fn handle_blockio_write(&self, request: &BlockFifoRequest) -> Result<(), Error> {
        let block_size = self.file.get_block_size();
        let mut data = {
            let vmos = self.vmos.lock().unwrap();
            let vmo = vmos.get(&request.vmoid).ok_or(FxfsError::NotFound)?;
            let mut buffer = vec![0u8; (request.block_count as u64 * block_size) as usize];
            vmo.read(&mut buffer[..], request.vmo_block)?;
            buffer
        };

        self.file.write_at(request.device_block * block_size as u64, &mut data[..]).await?;

        Ok(())
    }

    async fn handle_blockio_read(&self, request: &BlockFifoRequest) -> Result<(), Error> {
        let block_size = self.file.get_block_size();

        let mut buffer = vec![0u8; (request.block_count as u64 * block_size) as usize];
        let bytes_read =
            self.file.read_at(request.device_block * (block_size as u64), &mut buffer[..]).await?;

        // Fill in the rest of the buffer if bytes_read is less than the requested amount
        buffer[bytes_read as usize..].fill(0);

        let vmos = self.vmos.lock().unwrap();
        let vmo = vmos.get(&request.vmoid).ok_or(FxfsError::NotFound)?;
        vmo.write(&buffer[..], request.vmo_block)?;

        Ok(())
    }

    pub async fn run(&mut self) -> Result<(), Error> {
        fn into_raw_status(result: Result<(), Error>) -> zx::sys::zx_status_t {
            let status: zx::Status = result.map_err(|e| map_to_status(e)).into();
            status.into_raw()
        }

        let server = fidl::endpoints::ServerEnd::<block::BlockMarker>::new(
            self.server_channel.take().unwrap(),
        );

        // Create a fifo
        let (server_fifo, client_fifo) =
            zx::Fifo::create(16, std::mem::size_of::<BlockFifoRequest>())?;
        let maybe_server_fifo = std::sync::Mutex::new(Some(client_fifo));

        // Handling requests from fifo
        let fifo_future = async {
            let fifo = fasync::Fifo::<BlockFifoRequest, BlockFifoResponse>::from_fifo(server_fifo)?;
            while let Some(request) = fifo.read_entry().await? {
                match request.op_code {
                    remote_block_device::BLOCKIO_CLOSE_VMO => {
                        let status = {
                            let mut vmos = self.vmos.lock().unwrap();
                            match vmos.remove(&request.vmoid) {
                                Some(_vmo) => zx::Status::OK.into_raw(),
                                None => zx::Status::NOT_FOUND.into_raw(),
                            }
                        };
                        let response = BlockFifoResponse {
                            status,
                            request_id: request.request_id,
                            ..Default::default()
                        };
                        fifo.write_entries(std::slice::from_ref(&response)).await?;
                    }
                    remote_block_device::BLOCKIO_WRITE => {
                        let response = BlockFifoResponse {
                            status: into_raw_status(self.handle_blockio_write(&request).await),
                            request_id: request.request_id,
                            ..Default::default()
                        };
                        fifo.write_entries(std::slice::from_ref(&response)).await?;
                    }
                    remote_block_device::BLOCKIO_READ => {
                        let response = BlockFifoResponse {
                            status: into_raw_status(self.handle_blockio_read(&request).await),
                            request_id: request.request_id,
                            ..Default::default()
                        };
                        fifo.write_entries(std::slice::from_ref(&response)).await?;
                    }
                    _ => panic!("Unexpected message"),
                }
            }
            Result::<_, Error>::Ok(())
        };

        // Handling requests from fidl
        let channel_future = async {
            server
                .into_stream()?
                .map_err(|e| e.into())
                .try_for_each(|request| self.handle_request(request, &maybe_server_fifo))
                .await?;
            // This is temporary for when client doesn't call for fifo
            maybe_server_fifo.lock().unwrap().take();
            Ok(())
        };
        try_join!(fifo_future, channel_future)?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        crate::server::testing::TestFixture,
        anyhow::Error,
        fidl::endpoints::ServerEnd,
        fidl_fuchsia_io::{
            MODE_TYPE_BLOCK_DEVICE, OPEN_FLAG_CREATE, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
        },
        fuchsia_async as fasync, fuchsia_zircon as zx,
        futures::try_join,
        remote_block_device::{BlockClient, RemoteBlockClient, VmoId},
        std::collections::HashSet,
    };

    #[fasync::run(10, test)]
    async fn test_block_server() {
        let (client_channel, server_channel) =
            zx::Channel::create().expect("Channel::create failed");
        try_join!(
            async {
                let _remote_block_device = RemoteBlockClient::new(client_channel).await?;
                Result::<_, Error>::Ok(())
            },
            async {
                let fixture = TestFixture::new().await;
                let root = fixture.root();
                root.open(
                    OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
                    MODE_TYPE_BLOCK_DEVICE,
                    "foo",
                    ServerEnd::new(server_channel),
                )
                .expect("open failed");

                fixture.close().await;
                Ok(())
            }
        )
        .expect("client failed");
    }

    #[fasync::run(10, test)]
    async fn test_attach_vmo() {
        let (client_channel, server_channel) =
            zx::Channel::create().expect("Channel::create failed");
        try_join!(
            async {
                let remote_block_device = RemoteBlockClient::new(client_channel).await?;
                let mut vmo_set = HashSet::new();
                let vmo = zx::Vmo::create(1)?;
                for _ in 1..5 {
                    match remote_block_device.attach_vmo(&vmo).await {
                        Ok(vmo_id) => {
                            // TODO(fxbug.dev/89873): need to detach vmoid. into_id() is a
                            // temporary solution. Remove this after detaching vmo has been
                            // implemented
                            // Make sure that vmo_id is unique
                            assert_eq!(vmo_set.insert(vmo_id.into_id()), true);
                        }
                        Err(e) => panic!("unexpected error {:?}", e),
                    }
                }
                Result::<_, Error>::Ok(())
            },
            async {
                let fixture = TestFixture::new().await;
                let root = fixture.root();
                root.open(
                    OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
                    MODE_TYPE_BLOCK_DEVICE,
                    "foo",
                    ServerEnd::new(server_channel),
                )
                .expect("open failed");

                fixture.close().await;
                Ok(())
            }
        )
        .expect("client failed");
    }

    #[fasync::run(10, test)]
    async fn test_detach_vmo() {
        let (client_channel, server_channel) =
            zx::Channel::create().expect("Channel::create failed");
        try_join!(
            async {
                let remote_block_device = RemoteBlockClient::new(client_channel).await?;
                let vmo = zx::Vmo::create(1)?;
                let vmo_id = remote_block_device.attach_vmo(&vmo).await?;
                let vmo_id_copy = VmoId::new(vmo_id.id());
                remote_block_device.detach_vmo(vmo_id).await.expect("detach failed");
                remote_block_device.detach_vmo(vmo_id_copy).await.expect_err("detach succeeded");
                Result::<_, Error>::Ok(())
            },
            async {
                let fixture = TestFixture::new().await;
                let root = fixture.root();
                root.open(
                    OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
                    MODE_TYPE_BLOCK_DEVICE,
                    "foo",
                    ServerEnd::new(server_channel),
                )
                .expect("open failed");

                fixture.close().await;
                Ok(())
            }
        )
        .expect("client failed");
    }

    #[fasync::run(10, test)]
    async fn test_read_write_files() {
        let (client_channel, server_channel) =
            zx::Channel::create().expect("Channel::create failed");
        try_join!(
            async {
                let remote_block_device = RemoteBlockClient::new(client_channel).await?;
                let vmo = zx::Vmo::create(131072).expect("create vmo failed");
                let vmo_id = remote_block_device.attach_vmo(&vmo).await.expect("attach_vmo failed");

                // Must write with length as a multiple of the block_size
                let offset = remote_block_device.block_size() as usize;
                let len = remote_block_device.block_size() as usize;
                let write_buf = vec![0xa3u8; len];
                remote_block_device
                    .write_at(write_buf[..].into(), offset as u64)
                    .await
                    .expect("write_at failed");

                // Read back an extra block either side
                let mut read_buf = vec![0u8; len + 2 * remote_block_device.block_size() as usize];
                remote_block_device
                    .read_at(
                        read_buf.as_mut_slice().into(),
                        offset as u64 - remote_block_device.block_size() as u64,
                    )
                    .await
                    .expect("read_at failed");

                // We expect the extra block on the LHS of the read_buf to be 0
                assert_eq!(&read_buf[..offset], &vec![0; offset][..]);
                assert_eq!(&read_buf[offset..offset + len], &write_buf);
                // We expect the extra block on the RHS of the read_buf to be 0
                assert_eq!(
                    &read_buf[offset + len..],
                    &vec![0; remote_block_device.block_size() as usize][..]
                );

                remote_block_device.detach_vmo(vmo_id).await.expect("detach failed");
                Result::<_, Error>::Ok(())
            },
            async {
                let fixture = TestFixture::new().await;
                let root = fixture.root();
                root.open(
                    OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
                    MODE_TYPE_BLOCK_DEVICE,
                    "foo",
                    ServerEnd::new(server_channel),
                )
                .expect("open failed");

                fixture.close().await;
                Ok(())
            }
        )
        .expect("client failed");
    }
}
