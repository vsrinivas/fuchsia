// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::flashmap::Flashmap,
    anyhow::{Context, Error},
    fidl::endpoints::Proxy,
    fidl_fuchsia_device::ControllerProxy,
    fidl_fuchsia_nand::BrokerProxy,
    fidl_fuchsia_nand_flashmap::{FlashmapMarker, ManagerRequest, ManagerRequestStream},
    fuchsia_syslog::fx_log_warn,
    fuchsia_zircon as zx,
    futures::lock::Mutex,
    futures::TryStreamExt,
    std::{collections::HashMap, sync::Arc},
};

pub struct Manager {
    address_hint: Option<u64>,
    instances: Mutex<HashMap<String, Arc<Flashmap>>>,
}

impl Manager {
    pub fn new(address_hint: Option<u64>) -> Self {
        Manager { address_hint, instances: Mutex::new(HashMap::new()) }
    }

    pub async fn serve(&self, mut stream: ManagerRequestStream) {
        while let Ok(Some(req)) = stream.try_next().await {
            match req {
                ManagerRequest::Start { device, server, .. } => {
                    let control = ControllerProxy::new(
                        fidl::AsyncChannel::from_channel(device.into_channel()).unwrap(),
                    );
                    let path = Self::get_topo_path(&control).await.unwrap();
                    let broker = BrokerProxy::new(control.into_channel().unwrap());
                    if let Err(e) = self.find_or_start(broker, path, server).await {
                        fx_log_warn!("failed to find fmap: {:?}", e);
                    }
                }
            }
        }
    }

    async fn get_topo_path(controller: &ControllerProxy) -> Result<String, Error> {
        match controller.get_topological_path().await.context("sending FIDL get topo path")? {
            Err(st) => {
                zx::ok(st).context("getting topo path")?;
                unreachable!();
            }
            Ok(path) => Ok(path),
        }
    }

    async fn find_or_start(
        &self,
        broker: BrokerProxy,
        path: String,
        server: fidl::endpoints::ServerEnd<FlashmapMarker>,
    ) -> Result<(), Error> {
        let flashmap = {
            let mut lock = self.instances.lock().await;
            if let Some(value) = lock.get(&path) {
                Arc::clone(&value)
            } else {
                let flashmap = Arc::new(Flashmap::new(broker, self.address_hint).await?);
                lock.insert(path, Arc::clone(&flashmap));
                flashmap
            }
        };
        let server = server.into_stream().context("Getting stream")?;
        fuchsia_async::Task::spawn(async move {
            match flashmap.serve(server).await {
                Ok(()) => {}
                Err(e) => fx_log_warn!("Failed while serving flashmap requests: {:?}", e),
            };
        })
        .detach();

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::flashmap::tests::FakeNandDevice, fidl_fuchsia_device::ControllerMarker,
        fidl_fuchsia_nand_flashmap::ManagerMarker, fuchsia_async::Task,
    };

    #[fuchsia::test]
    async fn test_manager_start() {
        let (manager, stream) =
            fidl::endpoints::create_proxy_and_stream::<ManagerMarker>().unwrap();
        Task::spawn(async move {
            let manager = Manager::new(None);
            manager.serve(stream).await;
        })
        .detach();

        let nand = Arc::new(FakeNandDevice::new_with_flashmap());
        let clone = Arc::clone(&nand);
        let (device, server) =
            fidl::endpoints::create_request_stream::<ControllerMarker>().unwrap();
        Task::spawn(async move {
            clone.serve_topo_path(server, "device1".to_string()).await.expect("Serve OK");
        })
        .detach();

        let (flashmap, server) = fidl::endpoints::create_proxy::<FlashmapMarker>().unwrap();

        manager.start(device.into_channel().into(), server).expect("FIDL write succeeds");

        assert_eq!(flashmap.get_areas().await.expect("Send FIDL OK").len(), 1);
    }

    #[fuchsia::test]
    async fn test_manager_caches_flashmap() {
        let (manager, stream) =
            fidl::endpoints::create_proxy_and_stream::<ManagerMarker>().unwrap();
        Task::spawn(async move {
            let manager = Manager::new(None);
            manager.serve(stream).await;
        })
        .detach();

        let nand = Arc::new(FakeNandDevice::new_with_flashmap());

        // A helper to make setting up a new fake device easier.
        let make_device = || {
            let (device, server) =
                fidl::endpoints::create_request_stream::<ControllerMarker>().unwrap();
            let clone = Arc::clone(&nand);
            Task::spawn(async move {
                clone.serve_topo_path(server, "device1".to_string()).await.expect("Serve OK");
            })
            .detach();
            device
        };

        let (flashmap, server) = fidl::endpoints::create_proxy::<FlashmapMarker>().unwrap();

        manager.start(make_device().into_channel().into(), server).expect("FIDL write succeeds");

        assert_eq!(flashmap.get_areas().await.expect("Send FIDL OK").len(), 1);

        let reads = nand.stats().await.reads;

        // Connect to the manager again.
        let (flashmap2, server2) = fidl::endpoints::create_proxy::<FlashmapMarker>().unwrap();
        manager.start(make_device().into_channel().into(), server2).expect("FIDL write succeeds");
        assert_eq!(flashmap2.get_areas().await.expect("Send FIDL OK").len(), 1);

        // The manager should have cached the flashmap information.
        assert_eq!(nand.stats().await.reads, reads);
    }
}
