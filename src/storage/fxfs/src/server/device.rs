// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::{bail, Error},
    fidl_fuchsia_hardware_block::{self as block, BlockRequest},
    fuchsia_async::{self as fasync, FifoReadable, FifoWritable},
    fuchsia_zircon as zx,
    futures::{stream::TryStreamExt, try_join},
    remote_block_device::{BlockFifoRequest, BlockFifoResponse},
    std::{collections::BTreeMap, option::Option, sync::Mutex},
};

/// Implements server to handle Block requests
pub struct BlockServer {
    server_channel: Option<zx::Channel>,
    vmos: Mutex<BTreeMap<u16, zx::Vmo>>,
    // TODO(fxbug.dev/89873) include fxfs file, file: Arc<FxFile>
}

impl BlockServer {
    /// Creates a new BlockServer given a server channel to listen on.
    pub fn new(server_channel: zx::Channel) -> BlockServer {
        BlockServer { server_channel: Some(server_channel), vmos: Mutex::new(BTreeMap::new()) }
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
                    block_size: 512,
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

    pub async fn run(&mut self) -> Result<(), Error> {
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
        super::BlockServer,
        fuchsia_async as fasync, fuchsia_zircon as zx,
        futures::try_join,
        remote_block_device::{BlockClient, RemoteBlockClient, VmoId},
        std::collections::HashSet,
    };

    #[fasync::run(10, test)]
    async fn test_block_server() {
        let (client, server) = zx::Channel::create().expect("Channel::create failed");
        try_join!(
            async {
                let _remote_block_device = RemoteBlockClient::new(client).await?;
                Result::<_, anyhow::Error>::Ok(())
            },
            async {
                let mut server = BlockServer::new(server);
                server.run().await.expect("server fail");
                Ok(())
            }
        )
        .expect("client failed");
    }

    #[fasync::run(10, test)]
    async fn test_attach_vmo() {
        let (client, server) = zx::Channel::create().expect("Channel::create failed");
        try_join!(
            async {
                let remote_block_device = RemoteBlockClient::new(client).await?;
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
                Result::<_, anyhow::Error>::Ok(())
            },
            async {
                let mut server = BlockServer::new(server);
                server.run().await.expect("server failed");
                Ok(())
            }
        )
        .expect("client failed");
    }

    #[fasync::run(10, test)]
    async fn test_detach_vmo() {
        let (client, server) = zx::Channel::create().expect("Channel::create failed");
        try_join!(
            async {
                let remote_block_device = RemoteBlockClient::new(client).await?;
                let vmo = zx::Vmo::create(1)?;
                let vmo_id = remote_block_device.attach_vmo(&vmo).await?;
                let vmo_id_copy = VmoId::new(vmo_id.id());
                remote_block_device.detach_vmo(vmo_id).await.expect("detach failed");
                remote_block_device.detach_vmo(vmo_id_copy).await.expect_err("detach succeeded");
                Result::<_, anyhow::Error>::Ok(())
            },
            async {
                let mut server = BlockServer::new(server);
                server.run().await.expect("server failed");
                Ok(())
            }
        )
        .expect("client failed");
    }

    // TODO(fxbug.dev/89873): test reading/writing to fxfs file
}
