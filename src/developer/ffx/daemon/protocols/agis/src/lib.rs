// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::anyhow, anyhow::Result, async_net::unix::UnixStream, async_trait::async_trait,
    fidl_fuchsia_developer_ffx as ffx, fidl_fuchsia_gpu_agis as agis, protocols::prelude::*,
    std::path::Path,
};

#[ffx_protocol]
#[derive(Default, Debug)]

pub struct ListenerProtocol {
    task_manager: tasks::TaskManager,
}

#[async_trait(?Send)]
impl FidlProtocol for ListenerProtocol {
    type Protocol = ffx::ListenerMarker;

    // Use a singleton within the daemon.
    type StreamHandler = FidlStreamHandler<Self>;

    async fn handle(
        self: &ListenerProtocol,
        ctx: &Context,
        req: ffx::ListenerRequest,
    ) -> Result<(), anyhow::Error> {
        match req {
            ffx::ListenerRequest::Listen { responder, target_query, global_id } => {
                // Retrieve the FfxBridge proxy.
                let (_target, ffx_bridge) = ctx
                    .open_target_proxy_with_info::<agis::FfxBridgeMarker>(
                        target_query.string_matcher,
                        "core/agis:out:fuchsia.gpu.agis.FfxBridge",
                    )
                    .await?;

                // Retrieve the |ffx_socket| endpoint from the |ffx_bridge|.
                let ffx_socket = match ffx_bridge.get_socket(global_id).await? {
                    Ok(socket) => fidl::handle::AsyncSocket::from_socket(socket)?,
                    Err(e) => {
                        responder.send(&mut Err(e.clone()))?;
                        return Err(anyhow!("FfxBridge::GetSocket error: {:?}", e));
                    }
                };

                self.task_manager.spawn(async move {
                    // Use async_net to establish a UnixStream and copy between ffx and unix
                    // sockets.

                    // Construct unix socket path.
                    let mut path: String = "/tmp/agis".to_owned();
                    path.push_str(&global_id.to_string());
                    let socket_path = Path::new(&path);

                    let unix_stream = UnixStream::connect(socket_path).await.unwrap();

                    // Split sockets for bi-directional communication.
                    let (read_half_ffx, mut write_half_ffx) =
                        futures::AsyncReadExt::split(ffx_socket);
                    let (read_half_unix, mut write_half_unix) =
                        futures::AsyncReadExt::split(unix_stream);

                    let unix_reader = futures::io::BufReader::with_capacity(
                        65536, /* buf size */
                        read_half_unix,
                    );

                    let ffx_reader = futures::io::BufReader::with_capacity(
                        65536, /* buf size */
                        read_half_ffx,
                    );

                    let ffx_copier = async {
                        // Read from ffx side, write to unix side.
                        match futures::io::copy_buf(ffx_reader, &mut write_half_unix).await {
                            Ok(_) => {
                                tracing::info!("ffx_to_unix copy succeeded");
                            }
                            Err(_) => {
                                tracing::error!("agis daemon: ffx_to_unix copy failed");
                            }
                        }
                    };

                    // Read from unix side, write to ffx side.
                    let unix_copier = async {
                        match futures::io::copy_buf(unix_reader, &mut write_half_ffx).await {
                            Ok(_) => {
                                tracing::info!("unix_to_ffx copy succeeded");
                            }
                            Err(_) => {
                                tracing::error!("agis daemon: unix_to_ffx copy failed");
                            }
                        }
                    };

                    let (_, _) = futures::join!(ffx_copier, unix_copier);
                });

                responder.send(&mut Ok(()))?;
                Ok(())
            }

            ffx::ListenerRequest::Shutdown { responder } => {
                if self.task_manager.num_tasks() == 0 {
                    tracing::info!("no tasks to cancel");
                    return Ok(());
                }
                let tasks = self.task_manager.drain();
                for t in tasks {
                    tracing::info!("cancelling task {:?}", t);
                    t.cancel().await;
                }
                responder.send(&mut Ok(()))?;
                Ok(())
            }
        }
    }
}
