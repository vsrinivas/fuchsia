// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, bail, Result},
    async_utils::async_once::Once,
    ffx_daemon_events::TargetConnectionState,
    ffx_daemon_target::fastboot::Fastboot,
    ffx_daemon_target::target::Target,
    ffx_daemon_target::zedboot::{reboot, reboot_to_bootloader, reboot_to_recovery},
    fidl::endpoints::ServerEnd,
    fidl::Error,
    fidl_fuchsia_developer_ffx::{
        self as ffx, RebootListenerRequest, TargetRebootError, TargetRebootResponder,
        TargetRebootState,
    },
    fidl_fuchsia_developer_remotecontrol::RemoteControlProxy,
    fidl_fuchsia_hardware_power_statecontrol::{AdminMarker, AdminProxy, RebootReason},
    futures::TryStreamExt,
    futures::{try_join, TryFutureExt},
    selectors::{self, VerboseError},
    std::rc::Rc,
    tasks::TaskManager,
};

const ADMIN_SELECTOR: &'static str =
    "bootstrap/power_manager:expose:fuchsia.hardware.power.statecontrol.Admin";

pub(crate) struct RebootController {
    target: Rc<Target>,
    remote_proxy: Once<RemoteControlProxy>,
    fastboot_proxy: Once<ffx::FastbootProxy>,
    admin_proxy: Once<AdminProxy>,
    tasks: TaskManager,
}

impl RebootController {
    pub(crate) fn new(target: Rc<Target>) -> Self {
        Self {
            target,
            remote_proxy: Once::new(),
            fastboot_proxy: Once::new(),
            admin_proxy: Once::new(),
            tasks: Default::default(),
        }
    }

    async fn get_remote_proxy(&self) -> Result<RemoteControlProxy> {
        // TODO(awdavies): Factor out init_remote_proxy from the target, OR
        // move the impl(s) here that rely on remote control to use init_remote_proxy
        // instead.
        self.remote_proxy
            .get_or_try_init(self.target.init_remote_proxy())
            .await
            .map(|proxy| proxy.clone())
    }

    async fn get_fastboot_proxy(&self) -> Result<ffx::FastbootProxy> {
        self.fastboot_proxy.get_or_try_init(self.fastboot_init()).await.map(|p| p.clone())
    }

    async fn get_admin_proxy(&self) -> Result<AdminProxy> {
        self.admin_proxy.get_or_try_init(self.init_admin_proxy()).await.map(|p| p.clone())
    }

    async fn init_admin_proxy(&self) -> Result<AdminProxy> {
        let (proxy, server_end) =
            fidl::endpoints::create_proxy::<AdminMarker>().map_err(|e| anyhow!(e))?;
        let selector = selectors::parse_selector::<VerboseError>(ADMIN_SELECTOR)?;
        self.get_remote_proxy()
            .await?
            .connect(selector, server_end.into_channel())
            .await?
            .map(|_| proxy)
            .map_err(|_| anyhow!("could not get admin proxy"))
    }

    async fn fastboot_init(&self) -> Result<ffx::FastbootProxy> {
        let (proxy, fastboot) = fidl::endpoints::create_proxy::<ffx::FastbootMarker>()?;
        self.spawn_fastboot(fastboot).await?;
        Ok(proxy)
    }

    pub(crate) async fn spawn_fastboot(
        &self,
        fastboot: ServerEnd<ffx::FastbootMarker>,
    ) -> Result<()> {
        let mut fastboot_manager = Fastboot::new(self.target.clone());
        let stream = fastboot.into_stream()?;
        self.tasks.spawn(async move {
            match fastboot_manager.handle_fastboot_requests_from_stream(stream).await {
                Ok(_) => tracing::trace!("Fastboot proxy finished - client disconnected"),
                Err(e) => tracing::error!("Handling fastboot requests: {:?}", e),
            }
        });
        Ok(())
    }

    pub(crate) async fn reboot(
        &self,
        state: TargetRebootState,
        responder: TargetRebootResponder,
    ) -> Result<()> {
        match self.target.get_connection_state() {
            TargetConnectionState::Fastboot(_) => match state {
                TargetRebootState::Product => {
                    match self.get_fastboot_proxy().await?.reboot().await? {
                        Ok(_) => responder.send(&mut Ok(())).map_err(Into::into),
                        Err(e) => {
                            tracing::error!("Fastboot communication error: {:?}", e);
                            responder
                                .send(&mut Err(TargetRebootError::FastbootCommunication))
                                .map_err(Into::into)
                        }
                    }
                }
                TargetRebootState::Bootloader => {
                    let (reboot_client, reboot_server) =
                        fidl::endpoints::create_endpoints::<ffx::RebootListenerMarker>()?;
                    let mut stream = reboot_server.into_stream()?;
                    match try_join!(
                        self.get_fastboot_proxy().await?.reboot_bootloader(reboot_client).map_err(
                            |e| anyhow!("fidl error when rebooting to bootloader: {:?}", e)
                        ),
                        async move {
                            if let Some(RebootListenerRequest::OnReboot { control_handle: _ }) =
                                stream.try_next().await?
                            {
                                Ok(())
                            } else {
                                bail!("Did not receive reboot signal");
                            }
                        }
                    ) {
                        Ok(_) => responder.send(&mut Ok(())).map_err(Into::into),
                        Err(e) => {
                            tracing::error!("Fastboot communication error: {:?}", e);
                            responder
                                .send(&mut Err(TargetRebootError::FastbootCommunication))
                                .map_err(Into::into)
                        }
                    }
                }
                TargetRebootState::Recovery => responder
                    .send(&mut Err(TargetRebootError::FastbootToRecovery))
                    .map_err(Into::into),
            },
            TargetConnectionState::Zedboot(_) => {
                let mut response = if let Some(addr) = self.target.netsvc_address() {
                    match state {
                        TargetRebootState::Product => reboot(addr).await.map(|_| ()).map_err(|e| {
                            tracing::error!("zedboot reboot failed {:?}", e);
                            TargetRebootError::NetsvcCommunication
                        }),
                        TargetRebootState::Bootloader => {
                            reboot_to_bootloader(addr).await.map(|_| ()).map_err(|e| {
                                tracing::error!("zedboot reboot to bootloader failed {:?}", e);
                                TargetRebootError::NetsvcCommunication
                            })
                        }
                        TargetRebootState::Recovery => {
                            reboot_to_recovery(addr).await.map(|_| ()).map_err(|e| {
                                tracing::error!("zedboot reboot to recovery failed {:?}", e);
                                TargetRebootError::NetsvcCommunication
                            })
                        }
                    }
                } else {
                    Err(TargetRebootError::NetsvcAddressNotFound)
                };
                responder.send(&mut response).map_err(Into::into)
            }
            // Everything else use AdminProxy
            _ => {
                let admin_proxy = match self
                    .get_admin_proxy()
                    .map_err(|e| {
                        tracing::warn!("error getting admin proxy: {}", e);
                        TargetRebootError::TargetCommunication
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
                            Ok(_) => responder.send(&mut Ok(())).map_err(Into::into),
                            Err(e) => handle_fidl_connection_err(e, responder).map_err(Into::into),
                        }
                    }
                    TargetRebootState::Bootloader => {
                        match admin_proxy.reboot_to_bootloader().await {
                            Ok(_) => responder.send(&mut Ok(())).map_err(Into::into),
                            Err(e) => handle_fidl_connection_err(e, responder).map_err(Into::into),
                        }
                    }
                    TargetRebootState::Recovery => match admin_proxy.reboot_to_recovery().await {
                        Ok(_) => responder.send(&mut Ok(())).map_err(Into::into),
                        Err(e) => handle_fidl_connection_err(e, responder).map_err(Into::into),
                    },
                }
            }
        }
    }
}

pub(crate) fn handle_fidl_connection_err(e: Error, responder: TargetRebootResponder) -> Result<()> {
    match e {
        Error::ClientChannelClosed { .. } => {
            tracing::warn!(
                "Reboot returned a client channel closed - assuming reboot succeeded: {:?}",
                e
            );
            responder.send(&mut Ok(()))?;
        }
        _ => {
            tracing::error!("Target communication error: {:?}", e);
            responder.send(&mut Err(TargetRebootError::TargetCommunication))?;
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::anyhow,
        fidl::endpoints::{create_proxy_and_stream, RequestStream},
        fidl_fuchsia_developer_ffx::{
            FastbootMarker, FastbootProxy, FastbootRequest, TargetMarker, TargetProxy,
            TargetRequest,
        },
        fidl_fuchsia_developer_remotecontrol::{RemoteControlMarker, RemoteControlRequest},
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
                        responder.send(&mut Ok(())).unwrap();
                    }
                    _ => assert!(false),
                }
            }
        })
        .detach();
        proxy
    }

    async fn setup() -> (Rc<Target>, TargetProxy) {
        let target = Target::new_named("scooby-dooby-doo");
        let fastboot_proxy = Once::new();
        let _ = fastboot_proxy.get_or_init(setup_fastboot()).await;
        let remote_proxy = Once::new();
        let _ = remote_proxy.get_or_init(setup_remote()).await;
        let admin_proxy = Once::new();
        let rc = RebootController {
            target: target.clone(),
            fastboot_proxy,
            remote_proxy,
            admin_proxy,
            tasks: Default::default(),
        };
        let (proxy, mut stream) = create_proxy_and_stream::<TargetMarker>().unwrap();
        fuchsia_async::Task::local(async move {
            while let Ok(Some(req)) = stream.try_next().await {
                match req {
                    TargetRequest::Reboot { state, responder } => {
                        rc.reboot(state, responder).await.unwrap();
                    }
                    r => panic!("received unexpected request {:?}", r),
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
        target.set_state(TargetConnectionState::Fastboot(Instant::now()));
        proxy
            .reboot(TargetRebootState::Product)
            .await?
            .map_err(|e| anyhow!("error rebooting: {:?}", e))
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_fastboot_reboot_recovery() -> Result<()> {
        let (target, proxy) = setup().await;
        target.set_state(TargetConnectionState::Fastboot(Instant::now()));
        assert!(proxy.reboot(TargetRebootState::Recovery).await?.is_err());
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_fastboot_reboot_bootloader() -> Result<()> {
        let (target, proxy) = setup().await;
        target.set_state(TargetConnectionState::Fastboot(Instant::now()));
        proxy
            .reboot(TargetRebootState::Bootloader)
            .await?
            .map_err(|e| anyhow!("error rebooting: {:?}", e))
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_zedboot_reboot_bootloader() -> Result<()> {
        let (target, proxy) = setup().await;
        target.set_state(TargetConnectionState::Zedboot(Instant::now()));
        assert!(proxy.reboot(TargetRebootState::Bootloader).await?.is_err());
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_zedboot_reboot_recovery() -> Result<()> {
        let (target, proxy) = setup().await;
        target.set_state(TargetConnectionState::Zedboot(Instant::now()));
        assert!(proxy.reboot(TargetRebootState::Recovery).await?.is_err());
        Ok(())
    }
}
