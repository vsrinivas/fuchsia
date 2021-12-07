// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_hardware_block::{self as block, BlockRequest},
    fuchsia_async::{self as fasync, FifoReadable},
    fuchsia_zircon as zx,
    futures::{
        future::{AbortHandle, Abortable},
        join,
        stream::StreamExt,
    },
    remote_block_device::BlockFifoRequest,
    remote_block_device::BlockFifoResponse,
};

/// Implements server to handle Block requests
pub struct BlockServer {
    server_channel: Option<zx::Channel>,
}

impl BlockServer {
    // Creates a new BlockServer given a server channel to listen on.
    pub fn new(server_channel: zx::Channel) -> BlockServer {
        BlockServer { server_channel: Some(server_channel) }
    }

    // Runs the server
    pub async fn run(&mut self) {
        let server = fidl::endpoints::ServerEnd::<block::BlockMarker>::new(
            self.server_channel.take().unwrap(),
        );

        // Create a fifo
        let (server_fifo, client_fifo) =
            zx::Fifo::create(16, std::mem::size_of::<BlockFifoRequest>())
                .expect("Fifo::create failed");
        let maybe_server_fifo = std::sync::Mutex::new(Some(client_fifo));

        // Handle requests from fifo
        let (fifo_future_abort, fifo_future_abort_registration) = AbortHandle::new_pair();
        let fifo_future = Abortable::new(
            async {
                let fifo =
                    fasync::Fifo::<BlockFifoRequest, BlockFifoResponse>::from_fifo(server_fifo)
                        .expect("from_fifo failed");
                while let Some(_request) = fifo.read_entry().await.expect("read_entry failed") {
                    // TODO(fxbug.dev/89873): handle requests from fifo
                }
            },
            fifo_future_abort_registration,
        );

        // Handle requests from fidl
        let channel_future = async {
            server
                .into_stream()
                .expect("into_stream failed")
                .for_each(|request| async {
                    let request = request.expect("unexpected fidl error");
                    // Print request for now ...
                    println!("{:?}", request);

                    match request {
                        BlockRequest::GetInfo { responder } => {
                            let mut block_info = block::BlockInfo {
                                block_count: 1024,
                                block_size: 512,
                                max_transfer_size: 1024 * 1024,
                                flags: 0,
                                reserved: 0,
                            };
                            responder
                                .send(zx::sys::ZX_OK, Some(&mut block_info))
                                .expect("send failed");
                        }
                        BlockRequest::GetFifo { responder } => {
                            responder
                                .send(zx::sys::ZX_OK, maybe_server_fifo.lock().unwrap().take())
                                .expect("send failed");
                        }
                        BlockRequest::AttachVmo { vmo: _, responder } => {
                            let mut vmo_id = block::VmoId { id: 1 };
                            responder.send(zx::sys::ZX_OK, Some(&mut vmo_id)).expect("send failed");
                        }
                        BlockRequest::CloseFifo { responder } => {
                            fifo_future_abort.abort();
                            responder.send(zx::sys::ZX_OK).expect("send failed");
                        }
                        _ => panic!("Unexpected message"),
                    }
                })
                .await;
            // This is temporary for when client doesn't call for fifo
            maybe_server_fifo.lock().unwrap().take();
        };
        let _result = join!(fifo_future, channel_future);
    }
}

#[cfg(test)]
mod tests {
    use {
        super::BlockServer, fuchsia_async as fasync, fuchsia_zircon as zx, futures::try_join,
        remote_block_device::RemoteBlockClient,
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
                server.run().await;
                Ok(())
            }
        )
        .expect("client failed");
    }
}
