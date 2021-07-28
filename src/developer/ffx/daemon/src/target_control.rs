// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::fastboot::Fastboot,
    crate::target::{ConnectionState, Target},
    crate::zedboot::{reboot, reboot_to_bootloader, reboot_to_recovery},
    anyhow::{anyhow, bail, Result},
    async_utils::async_once::Once,
    fidl::endpoints::create_endpoints,
    fidl::Error as FidlError,
    fidl_fuchsia_developer_bridge::{
        FastbootMarker, FastbootProxy, RebootListenerMarker, RebootListenerRequest,
        TargetControlRebootResponder, TargetControlRequest, TargetControlRequestStream,
        TargetRebootError, TargetRebootState,
    },
    fidl_fuchsia_developer_remotecontrol::RemoteControlProxy,
    fidl_fuchsia_hardware_power_statecontrol::{AdminMarker, AdminProxy, RebootReason},
    fuchsia_async::TimeoutExt,
    futures::prelude::*,
    futures::try_join,
    std::rc::Rc,
    std::time::Duration,
};

const ADMIN_SELECTOR: &str = "core/appmgr:out:fuchsia.hardware.power.statecontrol.Admin";

pub(crate) struct TargetControl {
    target: Rc<Target>,
    remote_proxy: Once<RemoteControlProxy>,
    fastboot_proxy: Once<FastbootProxy>,
    admin_proxy: Once<AdminProxy>,
}

impl TargetControl {
    pub(crate) fn new(target: Rc<Target>) -> Self {
        Self {
            target,
            remote_proxy: Once::new(),
            fastboot_proxy: Once::new(),
            admin_proxy: Once::new(),
        }
    }

    pub(crate) async fn handle_requests_from_stream(
        &mut self,
        mut stream: TargetControlRequestStream,
    ) -> Result<()> {
        while let Some(req) = stream.try_next().await? {
            self.handle_request(req).await?;
        }
        Ok(())
    }

    async fn handle_request(&mut self, req: TargetControlRequest) -> Result<()> {
        log::debug!("target control - received req: {:?}", req);
        match req {
            TargetControlRequest::Reboot { state, responder } => {
                match self.target.get_connection_state() {
                    ConnectionState::Fastboot(_) => match state {
                        TargetRebootState::Product => {
                            match self.get_fastboot_proxy().await?.reboot().await? {
                                Ok(_) => responder.send(&mut Ok(()))?,
                                Err(e) => {
                                    log::error!("Fastboot communication error: {:?}", e);
                                    responder
                                        .send(&mut Err(TargetRebootError::FastbootCommunication))?;
                                }
                            }
                        }
                        TargetRebootState::Bootloader => {
                            let (reboot_client, reboot_server) =
                                create_endpoints::<RebootListenerMarker>()?;
                            let mut stream = reboot_server.into_stream()?;
                            match try_join!(
                                self.get_fastboot_proxy()
                                    .await?
                                    .reboot_bootloader(reboot_client)
                                    .map_err(|e| anyhow!(
                                        "fidl error when rebooting to bootloader: {:?}",
                                        e
                                    )),
                                async move {
                                    if let Some(RebootListenerRequest::OnReboot {
                                        control_handle: _,
                                    }) = stream.try_next().await?
                                    {
                                        Ok(())
                                    } else {
                                        bail!("Did not receive reboot signal");
                                    }
                                }
                            ) {
                                Ok(_) => responder.send(&mut Ok(()))?,
                                Err(e) => {
                                    log::error!("Fastboot communication error: {:?}", e);
                                    responder
                                        .send(&mut Err(TargetRebootError::FastbootCommunication))?;
                                }
                            }
                        }
                        TargetRebootState::Recovery => {
                            responder.send(&mut Err(TargetRebootError::FastbootToRecovery))?;
                        }
                    },
                    ConnectionState::Zedboot(_) => {
                        let mut response = if let Some(addr) = self.target.netsvc_address() {
                            match state {
                                TargetRebootState::Product => {
                                    reboot(addr).await.map(|_| ()).map_err(|e| {
                                        log::error!("zedboot reboot failed {:?}", e);
                                        TargetRebootError::TargetCommunication
                                    })
                                }
                                TargetRebootState::Bootloader => {
                                    reboot_to_bootloader(addr).await.map(|_| ()).map_err(|e| {
                                        log::error!("zedboot reboot to bootloader failed {:?}", e);
                                        TargetRebootError::TargetCommunication
                                    })
                                }
                                TargetRebootState::Recovery => {
                                    reboot_to_recovery(addr).await.map(|_| ()).map_err(|e| {
                                        log::error!("zedboot reboot to recovery failed {:?}", e);
                                        TargetRebootError::TargetCommunication
                                    })
                                }
                            }
                        } else {
                            Err(TargetRebootError::TargetCommunication)
                        };
                        responder.send(&mut response)?;
                    }
                    // Everything else use AdminProxy
                    _ => {
                        const ADMIN_PROXY_TIMEOUT: Duration = Duration::from_secs(5);
                        let admin_proxy = match self
                            .get_admin_proxy()
                            .map_err(|e| {
                                log::warn!("error getting admin proxy: {}", e);
                                TargetRebootError::TargetCommunication
                            })
                            .on_timeout(ADMIN_PROXY_TIMEOUT, || {
                                log::warn!("timeout while getting admin proxy");
                                Err(TargetRebootError::TargetCommunication)
                            })
                            .await
                        {
                            Ok(a) => a,
                            Err(e) => {
                                responder.send(&mut Err(e))?;
                                return Err(anyhow!("failed to get admin proxy"));
                            }
                        };
                        match state {
                            TargetRebootState::Product => {
                                match admin_proxy.reboot(RebootReason::UserRequest).await {
                                    Ok(_) => responder.send(&mut Ok(()))?,
                                    Err(e) => handle_target_err(e, responder)?,
                                }
                            }
                            TargetRebootState::Bootloader => {
                                match admin_proxy.reboot_to_bootloader().await {
                                    Ok(_) => responder.send(&mut Ok(()))?,
                                    Err(e) => handle_target_err(e, responder)?,
                                }
                            }
                            TargetRebootState::Recovery => {
                                match admin_proxy.reboot_to_recovery().await {
                                    Ok(_) => responder.send(&mut Ok(()))?,
                                    Err(e) => handle_target_err(e, responder)?,
                                }
                            }
                        }
                    }
                }
            }
        }
        Ok(())
    }

    async fn get_remote_proxy(&self) -> Result<RemoteControlProxy> {
        self.remote_proxy
            .get_or_try_init(self.target.init_remote_proxy())
            .await
            .map(|proxy| proxy.clone())
    }

    async fn get_fastboot_proxy(&self) -> Result<FastbootProxy> {
        self.fastboot_proxy
            .get_or_try_init(self.init_fastboot_proxy())
            .await
            .map(|proxy| proxy.clone())
    }

    async fn init_fastboot_proxy(&self) -> Result<FastbootProxy> {
        let mut fastboot_manager = Fastboot::new(self.target.clone());
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<FastbootMarker>()?;
        fuchsia_async::Task::local(async move {
            match fastboot_manager.handle_fastboot_requests_from_stream(stream).await {
                Ok(_) => log::debug!("Fastboot proxy finished - client disconnected"),
                Err(e) => {
                    log::error!("There was an error handling fastboot requests: {:?}", e)
                }
            }
        })
        .detach();
        Ok(proxy)
    }

    async fn get_admin_proxy(&self) -> Result<AdminProxy> {
        self.admin_proxy.get_or_try_init(self.init_admin_proxy()).await.map(|proxy| proxy.clone())
    }

    async fn init_admin_proxy(&self) -> Result<AdminProxy> {
        let (proxy, server_end) =
            fidl::endpoints::create_proxy::<AdminMarker>().map_err(|e| anyhow!(e))?;
        let selector = selectors::parse_selector(ADMIN_SELECTOR)?;
        self.get_remote_proxy()
            .await?
            .connect(selector, server_end.into_channel())
            .await?
            .map(|_| proxy)
            .map_err(|_| anyhow!("could not get admin proxy"))
    }
}

fn handle_target_err(e: FidlError, responder: TargetControlRebootResponder) -> Result<()> {
    match e {
        FidlError::ClientChannelClosed { .. } => {
            log::warn!(
                "Reboot returned a client channel closed - assuming reboot succeeded: {:?}",
                e
            );
            responder.send(&mut Ok(()))?;
        }
        _ => {
            log::error!("Target communication error: {:?}", e);
            responder.send(&mut Err(TargetRebootError::TargetCommunication))?;
        }
    }
    Ok(())
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use {
        super::*,
        anyhow::anyhow,
        fidl::endpoints::{create_proxy_and_stream, RequestStream},
        fidl_fuchsia_developer_bridge::{FastbootRequest, TargetControlMarker, TargetControlProxy},
        fidl_fuchsia_developer_remotecontrol::{
            RemoteControlMarker, RemoteControlRequest, ServiceMatch,
        },
        fidl_fuchsia_hardware_power_statecontrol::{AdminRequest, AdminRequestStream},
        std::time::Instant,
    };

    async fn setup_fastboot() -> FastbootProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<FastbootMarker>().unwrap();
        fuchsia_async::Task::local(async move {
            while let Ok(Some(req)) = stream.try_next().await {
                match req {
                    FastbootRequest::Reboot { responder } => {
                        responder.send(&mut Ok(())).unwrap();
                    }
                    FastbootRequest::RebootBootloader { listener, responder } => {
                        listener.into_proxy().unwrap().on_reboot().unwrap();
                        responder.send(&mut Ok(())).unwrap();
                    }
                    _ => assert!(false),
                }
            }
        })
        .detach();
        proxy
    }

    fn setup_admin(chan: fidl::Channel) -> Result<()> {
        let mut stream = AdminRequestStream::from_channel(fidl::AsyncChannel::from_channel(chan)?);
        fuchsia_async::Task::local(async move {
            while let Ok(Some(req)) = stream.try_next().await {
                match req {
                    AdminRequest::Reboot { reason: RebootReason::UserRequest, responder } => {
                        responder.send(&mut Ok(())).unwrap();
                    }
                    AdminRequest::RebootToBootloader { responder } => {
                        responder.send(&mut Ok(())).unwrap();
                    }
                    AdminRequest::RebootToRecovery { responder } => {
                        responder.send(&mut Ok(())).unwrap();
                    }
                    _ => assert!(false),
                }
            }
        })
        .detach();
        Ok(())
    }

    async fn setup_remote() -> RemoteControlProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<RemoteControlMarker>().unwrap();
        fuchsia_async::Task::local(async move {
            while let Ok(Some(req)) = stream.try_next().await {
                match req {
                    RemoteControlRequest::Connect { selector: _, service_chan, responder } => {
                        setup_admin(service_chan).unwrap();
                        responder
                            .send(&mut Ok(ServiceMatch {
                                moniker: vec![],
                                subdir: String::default(),
                                service: String::default(),
                            }))
                            .unwrap();
                    }
                    _ => assert!(false),
                }
            }
        })
        .detach();
        proxy
    }

    async fn setup() -> (Rc<Target>, TargetControlProxy) {
        let target = Target::new_named("scooby-dooby-doo");
        let fastboot_proxy = Once::new();
        let _ = fastboot_proxy.get_or_init(setup_fastboot()).await;
        let remote_proxy = Once::new();
        let _ = remote_proxy.get_or_init(setup_remote()).await;
        let admin_proxy = Once::new();
        let mut tc =
            TargetControl { target: target.clone(), fastboot_proxy, remote_proxy, admin_proxy };
        let (proxy, stream) = create_proxy_and_stream::<TargetControlMarker>().unwrap();
        fuchsia_async::Task::local(async move {
            match tc.handle_requests_from_stream(stream).await {
                Ok(_) => log::debug!("Target control proxy finished - client disconnected"),
                Err(e) => {
                    panic!("There was an error handling target requests: {:?}", e);
                }
            }
        })
        .detach();
        (target, proxy)
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_reboot_product() -> Result<()> {
        let (_, proxy) = setup().await;
        proxy
            .reboot(TargetRebootState::Product)
            .await?
            .map_err(|e| anyhow!("error rebooting: {:?}", e))
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_reboot_recovery() -> Result<()> {
        let (_, proxy) = setup().await;
        proxy
            .reboot(TargetRebootState::Recovery)
            .await?
            .map_err(|e| anyhow!("error rebooting: {:?}", e))
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_reboot_bootloader() -> Result<()> {
        let (_, proxy) = setup().await;
        proxy
            .reboot(TargetRebootState::Bootloader)
            .await?
            .map_err(|e| anyhow!("error rebooting: {:?}", e))
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_fastboot_reboot_product() -> Result<()> {
        let (target, proxy) = setup().await;
        target.set_state(ConnectionState::Fastboot(Instant::now()));
        proxy
            .reboot(TargetRebootState::Product)
            .await?
            .map_err(|e| anyhow!("error rebooting: {:?}", e))
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_fastboot_reboot_recovery() -> Result<()> {
        let (target, proxy) = setup().await;
        target.set_state(ConnectionState::Fastboot(Instant::now()));
        assert!(proxy.reboot(TargetRebootState::Recovery).await?.is_err());
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_fastboot_reboot_bootloader() -> Result<()> {
        let (target, proxy) = setup().await;
        target.set_state(ConnectionState::Fastboot(Instant::now()));
        proxy
            .reboot(TargetRebootState::Bootloader)
            .await?
            .map_err(|e| anyhow!("error rebooting: {:?}", e))
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_zedboot_reboot_bootloader() -> Result<()> {
        let (target, proxy) = setup().await;
        target.set_state(ConnectionState::Zedboot(Instant::now()));
        assert!(proxy.reboot(TargetRebootState::Bootloader).await?.is_err());
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_zedboot_reboot_recovery() -> Result<()> {
        let (target, proxy) = setup().await;
        target.set_state(ConnectionState::Zedboot(Instant::now()));
        assert!(proxy.reboot(TargetRebootState::Recovery).await?.is_err());
        Ok(())
    }
}
