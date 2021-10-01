// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::fastboot::{
        continue_boot, erase, flash, get_all_vars, get_var, oem, reboot, reboot_bootloader,
        set_active, stage, UploadProgressListener, VariableListener,
    },
    crate::target::Target,
    crate::zedboot::reboot_to_bootloader,
    anyhow::{anyhow, bail, Context, Result},
    async_trait::async_trait,
    async_utils::async_once::Once,
    fastboot::UploadProgressListener as _,
    ffx_daemon_events::{TargetConnectionState, TargetEvent},
    fidl::Error as FidlError,
    fidl_fuchsia_developer_bridge::{
        FastbootError, FastbootRequest, FastbootRequestStream, RebootListenerProxy,
    },
    fidl_fuchsia_developer_remotecontrol::RemoteControlProxy,
    fidl_fuchsia_hardware_power_statecontrol::{AdminMarker, AdminProxy},
    futures::io::{AsyncRead, AsyncWrite},
    futures::prelude::*,
    futures::try_join,
    std::rc::Rc,
    std::time::Duration,
};

const ADMIN_SELECTOR: &str = "core/appmgr:out:fuchsia.hardware.power.statecontrol.Admin";

#[async_trait(?Send)]
pub trait InterfaceFactory<T: AsyncRead + AsyncWrite + Unpin> {
    async fn open(&mut self, target: &Target) -> Result<T>;
    async fn close(&self);
}

pub struct FastbootImpl<T: AsyncRead + AsyncWrite + Unpin> {
    pub target: Rc<Target>,
    pub interface: Option<T>,
    pub interface_factory: Box<dyn InterfaceFactory<T>>,
    remote_proxy: Once<RemoteControlProxy>,
    admin_proxy: Once<AdminProxy>,
}

impl<T: AsyncRead + AsyncWrite + Unpin> FastbootImpl<T> {
    pub fn new(target: Rc<Target>, interface_factory: Box<dyn InterfaceFactory<T>>) -> Self {
        Self {
            target,
            interface: None,
            interface_factory,
            remote_proxy: Once::new(),
            admin_proxy: Once::new(),
        }
    }

    async fn clear_interface(&mut self) {
        self.interface = None;
        self.interface_factory.close().await;
    }

    async fn interface(&mut self) -> Result<&mut T> {
        if self.interface.is_none() {
            self.interface.replace(self.interface_factory.open(&self.target).await?);
        }
        Ok(self.interface.as_mut().expect("interface interface not available"))
    }

    pub async fn handle_fastboot_requests_from_stream(
        &mut self,
        mut stream: FastbootRequestStream,
    ) -> Result<()> {
        while let Some(req) = stream.try_next().await? {
            match self.handle_fastboot_request(req).await {
                Ok(_) => (),
                Err(e) => {
                    self.clear_interface().await;
                    return Err(e);
                }
            }
        }
        // Make sure the serial is no longer in use.
        self.clear_interface().await;
        Ok(())
    }

    async fn reboot_from_zedboot(&self, listener: &RebootListenerProxy) -> Result<()> {
        listener.on_reboot()?;
        match self.target.netsvc_address() {
            Some(addr) => {
                reboot_to_bootloader(addr).await?;
                self.check_for_fastboot().await
            }
            None => bail!("Could not determine address to send Zedboot Reboot command"),
        }
    }

    fn is_target_in_fastboot(&self) -> bool {
        match self.target.get_connection_state() {
            TargetConnectionState::Fastboot(_) => true,
            _ => false,
        }
    }

    async fn check_for_fastboot(&self) -> Result<()> {
        for _ in 0..2 {
            if self.is_target_in_fastboot() {
                return Ok(());
            }
            // Even if it times out, just check if it's in fastboot.
            let _ = self
                .target
                .events
                .wait_for(Some(Duration::from_secs(10)), |e| e == TargetEvent::Rediscovered)
                .await;

            if self.is_target_in_fastboot() {
                return Ok(());
            }
        }
        bail!("Timed out checking for fastboot.")
    }

    async fn reboot_from_product(&self, listener: &RebootListenerProxy) -> Result<()> {
        listener.on_reboot()?;
        match self.get_admin_proxy().await?.reboot_to_bootloader().await {
            Ok(_) => self.check_for_fastboot().await,
            Err(_e @ FidlError::ClientChannelClosed { .. }) => self.check_for_fastboot().await,
            Err(e) => bail!(e),
        }
    }

    async fn prepare_device(&self, listener: &RebootListenerProxy) -> Result<()> {
        match self.target.get_connection_state() {
            TargetConnectionState::Fastboot(_) => Ok(()),
            TargetConnectionState::Zedboot(_) => self.reboot_from_zedboot(listener).await,
            _ => self.reboot_from_product(listener).await,
        }
    }

    async fn handle_fastboot_request(&mut self, req: FastbootRequest) -> Result<()> {
        log::debug!("fastboot - received req: {:?}", req);
        match req {
            FastbootRequest::Prepare { listener, responder } => {
                match self.prepare_device(&listener.into_proxy()?).await {
                    Ok(_) => responder.send(&mut Ok(()))?,
                    Err(e) => {
                        log::error!("Error preparing device: {}", e);
                        responder
                            .send(&mut Err(FastbootError::RebootFailed))
                            .context("sending error response")?;
                    }
                }
            }
            FastbootRequest::GetVar { name, responder } => {
                match get_var(self.interface().await?, &name).await {
                    Ok(value) => responder.send(&mut Ok(value))?,
                    Err(e) => {
                        log::error!("Error getting variable '{}': {:?}", name, e);
                        responder
                            .send(&mut Err(FastbootError::ProtocolError))
                            .context("sending error response")?;
                    }
                }
            }
            FastbootRequest::GetAllVars { listener, responder } => {
                let variable_listener = VariableListener::new(listener)?;
                match get_all_vars(self.interface().await?, &variable_listener).await {
                    Ok(()) => {
                        responder.send(&mut Ok(()))?;
                    }
                    Err(e) => {
                        log::error!("Error getting all variables: {:?}", e);
                        responder
                            .send(&mut Err(FastbootError::ProtocolError))
                            .context("sending error response")?;
                    }
                }
            }
            FastbootRequest::Flash { partition_name, path, listener, responder } => {
                let upload_listener = UploadProgressListener::new(listener)?;
                match flash(self.interface().await?, &path, &partition_name, &upload_listener).await
                {
                    Ok(_) => responder.send(&mut Ok(()))?,
                    Err(e) => {
                        log::error!(
                            "Error flashing \"{}\" from {}:\n{:?}",
                            partition_name,
                            path,
                            e
                        );
                        upload_listener.on_error(&format!("{}", e))?;
                        responder
                            .send(&mut Err(FastbootError::ProtocolError))
                            .context("sending error response")?;
                    }
                }
            }
            FastbootRequest::Erase { partition_name, responder } => {
                match erase(self.interface().await?, &partition_name).await {
                    Ok(_) => responder.send(&mut Ok(()))?,
                    Err(e) => {
                        log::error!("Error erasing \"{}\": {:?}", partition_name, e);
                        responder
                            .send(&mut Err(FastbootError::ProtocolError))
                            .context("sending error response")?;
                    }
                }
            }
            FastbootRequest::Reboot { responder } => match reboot(self.interface().await?).await {
                Ok(_) => responder.send(&mut Ok(()))?,
                Err(e) => {
                    log::error!("Error rebooting: {:?}", e);
                    responder
                        .send(&mut Err(FastbootError::ProtocolError))
                        .context("sending error response")?;
                }
            },
            FastbootRequest::RebootBootloader { listener, responder } => {
                match reboot_bootloader(self.interface().await?).await {
                    Ok(_) => {
                        self.clear_interface().await;
                        match try_join!(
                            self.target
                                .events
                                .wait_for(Some(Duration::from_secs(30)), |e| {
                                    e == TargetEvent::Rediscovered
                                })
                                .map_err(|_| FastbootError::RediscoveredError),
                            async move {
                                listener
                                    .into_proxy()
                                    .map_err(|_| FastbootError::CommunicationError)?
                                    .on_reboot()
                                    .map_err(|_| FastbootError::CommunicationError)
                            }
                        ) {
                            Ok(_) => {
                                log::debug!("Rediscovered reboot target");
                                responder.send(&mut Ok(()))?;
                            }
                            Err(e) => {
                                log::error!("Error rebooting and rediscovering target: {:?}", e);
                                // Check the target and see what state it's in.  Maybe we just
                                // missed the event.
                                match self.check_for_fastboot().await {
                                    Ok(_) => {
                                        log::debug!("Target in fastboot despite timeout.");
                                        responder.send(&mut Ok(()))?;
                                    }
                                    _ => responder
                                        .send(&mut Err(e))
                                        .context("sending error response")?,
                                }
                            }
                        }
                    }
                    Err(e) => {
                        log::error!("Error rebooting: {:?}", e);
                        responder
                            .send(&mut Err(FastbootError::ProtocolError))
                            .context("sending error response")?;
                    }
                }
            }
            FastbootRequest::ContinueBoot { responder } => {
                match continue_boot(self.interface().await?).await {
                    Ok(_) => responder.send(&mut Ok(()))?,
                    Err(e) => {
                        log::error!("Error continuing boot: {:?}", e);
                        responder
                            .send(&mut Err(FastbootError::ProtocolError))
                            .context("sending error response")?;
                    }
                }
            }
            FastbootRequest::SetActive { slot, responder } => {
                match set_active(self.interface().await?, &slot).await {
                    Ok(_) => responder.send(&mut Ok(()))?,
                    Err(e) => {
                        log::error!("Error setting active: {:?}", e);
                        responder
                            .send(&mut Err(FastbootError::ProtocolError))
                            .context("sending error response")?;
                    }
                }
            }
            FastbootRequest::Stage { path, listener, responder } => {
                match stage(self.interface().await?, &path, &UploadProgressListener::new(listener)?)
                    .await
                {
                    Ok(_) => responder.send(&mut Ok(()))?,
                    Err(e) => {
                        log::error!("Error setting active: {:?}", e);
                        responder
                            .send(&mut Err(FastbootError::ProtocolError))
                            .context("sending error response")?;
                    }
                }
            }
            FastbootRequest::Oem { command, responder } => {
                match oem(self.interface().await?, &command).await {
                    Ok(_) => responder.send(&mut Ok(()))?,
                    Err(e) => {
                        log::error!("Error sending oem \"{}\": {:?}", command, e);
                        responder
                            .send(&mut Err(FastbootError::ProtocolError))
                            .context("sending error response")?;
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

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use {
        super::*,
        fastboot::reply::Reply,
        fidl::endpoints::{create_endpoints, create_proxy_and_stream},
        fidl_fuchsia_developer_bridge::{
            FastbootError, FastbootMarker, FastbootProxy, RebootListenerMarker,
            RebootListenerRequest, UploadProgressListenerMarker,
        },
        futures::task::{Context as fContext, Poll},
        std::io::{BufWriter, Write},
        std::pin::Pin,
        tempfile::NamedTempFile,
    };

    struct TestTransport {
        replies: Vec<Reply>,
    }

    impl AsyncRead for TestTransport {
        fn poll_read(
            self: Pin<&mut Self>,
            _cx: &mut fContext<'_>,
            buf: &mut [u8],
        ) -> Poll<std::io::Result<usize>> {
            match self.get_mut().replies.pop() {
                Some(r) => {
                    let reply = Vec::<u8>::from(r);
                    buf[..reply.len()].copy_from_slice(&reply);
                    Poll::Ready(Ok(reply.len()))
                }
                None => Poll::Ready(Ok(0)),
            }
        }
    }

    impl AsyncWrite for TestTransport {
        fn poll_write(
            self: Pin<&mut Self>,
            _cx: &mut fContext<'_>,
            buf: &[u8],
        ) -> Poll<std::io::Result<usize>> {
            Poll::Ready(Ok(buf.len()))
        }

        fn poll_flush(self: Pin<&mut Self>, _cx: &mut fContext<'_>) -> Poll<std::io::Result<()>> {
            unimplemented!();
        }

        fn poll_close(self: Pin<&mut Self>, _cx: &mut fContext<'_>) -> Poll<std::io::Result<()>> {
            unimplemented!();
        }
    }

    impl TestTransport {
        pub fn new() -> Self {
            TestTransport { replies: Vec::new() }
        }

        pub fn push(&mut self, reply: Reply) {
            self.replies.push(reply);
        }
    }

    struct TestFactory {
        replies: Vec<Reply>,
    }

    impl TestFactory {
        pub fn new(replies: Vec<Reply>) -> Self {
            Self { replies }
        }
    }

    #[async_trait(?Send)]
    impl InterfaceFactory<TestTransport> for TestFactory {
        async fn open(&mut self, _target: &Target) -> Result<TestTransport> {
            let mut transport = TestTransport::new();
            self.replies.iter().rev().for_each(|r| transport.push(r.clone()));
            return Ok(transport);
        }

        async fn close(&self) {}
    }

    async fn setup(replies: Vec<Reply>) -> (Rc<Target>, FastbootProxy) {
        let target = Target::new_named("scooby-dooby-doo");
        let mut fb =
            FastbootImpl::<TestTransport>::new(target.clone(), Box::new(TestFactory::new(replies)));
        let (proxy, stream) = create_proxy_and_stream::<FastbootMarker>().unwrap();
        fuchsia_async::Task::local(async move {
            match fb.handle_fastboot_requests_from_stream(stream).await {
                Ok(_) => log::debug!("Fastboot proxy finished - client disconnected"),
                Err(e) => log::error!("There was an error handling fastboot requests: {:?}", e),
            }
            assert!(fb.interface.is_none());
        })
        .detach();
        (target, proxy)
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_flash() -> Result<()> {
        let (prog_client, prog_server) = create_endpoints::<UploadProgressListenerMarker>()?;
        let mut stream = prog_server.into_stream()?;
        let file: NamedTempFile = NamedTempFile::new().expect("tmp access failed");
        let mut buffer = BufWriter::new(&file);
        buffer.write_all(b"Test")?;
        buffer.flush()?;
        let (_, proxy) = setup(vec![
            Reply::Data(4),
            Reply::Okay("".to_string()), //Download Reply
            Reply::Okay("".to_string()), //Flash Reply
        ])
        .await;
        let filepath = file.path().to_str().ok_or(anyhow!("error getting tempfile path"))?;
        try_join!(
            async move {
                while let Some(_) = stream.try_next().await? { /* do nothing */ }
                Ok(())
            },
            proxy
                .flash("test", filepath, prog_client)
                .map_err(|e| anyhow!("error flashing: {:?}", e))
        )
        .and_then(|(_, flash)| {
            assert!(flash.is_ok());
            Ok(())
        })
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_flash_sends_protocol_error_after_unexpected_reply() -> Result<()> {
        let (prog_client, prog_server) = create_endpoints::<UploadProgressListenerMarker>()?;
        let mut stream = prog_server.into_stream()?;
        let file: NamedTempFile = NamedTempFile::new().expect("tmp access failed");
        let mut buffer = BufWriter::new(&file);
        buffer.write_all(b"Test")?;
        buffer.flush()?;
        let (_, proxy) = setup(vec![Reply::Data(6)]).await;
        let filepath = file.path().to_str().ok_or(anyhow!("error getting tempfile path"))?;
        try_join!(
            async move {
                while let Some(_) = stream.try_next().await? { /* do nothing */ }
                Ok(())
            },
            proxy
                .flash("test", filepath, prog_client)
                .map_err(|e| anyhow!("error flashing: {:?}", e))
        )
        .and_then(|(_, flash)| {
            assert_eq!(flash.err(), Some(FastbootError::ProtocolError));
            Ok(())
        })
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_erase() -> Result<()> {
        let (_, proxy) = setup(vec![Reply::Okay("".to_string())]).await;
        proxy.erase("test").await?.map_err(|e| anyhow!("error erase: {:?}", e))
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_erase_sends_protocol_error_after_unexpected_reply() -> Result<()> {
        let (_, proxy) = setup(vec![Reply::Fail("".to_string())]).await;
        let res = proxy.erase("test").await?;
        assert_eq!(res.err(), Some(FastbootError::ProtocolError));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_reboot() -> Result<()> {
        let (_, proxy) = setup(vec![Reply::Okay("".to_string())]).await;
        proxy.reboot().await?.map_err(|e| anyhow!("error reboot: {:?}", e))
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_reboot_sends_protocol_error_after_unexpected_reply() -> Result<()> {
        let (_, proxy) = setup(vec![Reply::Fail("".to_string())]).await;
        let res = proxy.reboot().await?;
        assert_eq!(res.err(), Some(FastbootError::ProtocolError));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_reboot_bootloader() -> Result<()> {
        let (reboot_client, reboot_server) = create_endpoints::<RebootListenerMarker>()?;
        let mut stream = reboot_server.into_stream()?;
        let (target, proxy) = setup(vec![Reply::Okay("".to_string())]).await;
        try_join!(
            async move {
                // Should only need to wait for the first request.
                if let Some(RebootListenerRequest::OnReboot { control_handle: _ }) =
                    stream.try_next().await?
                {
                    return target.events.push(TargetEvent::Rediscovered);
                }
                bail!("did not receive reboot event");
            },
            proxy
                .reboot_bootloader(reboot_client)
                .map_err(|e| anyhow!("error rebooting to bootloader: {:?}", e)),
        )
        .and_then(|(_, reboot)| {
            reboot.map_err(|e| anyhow!("failed booting to bootloader: {:?}", e))
        })
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_reboot_bootloader_sends_protocol_error_after_unexpected_reply() -> Result<()> {
        let (reboot_client, _) = create_endpoints::<RebootListenerMarker>()?;
        let (_, proxy) = setup(vec![Reply::Fail("".to_string())]).await;
        let res = proxy.reboot_bootloader(reboot_client).await?;
        assert_eq!(res.err(), Some(FastbootError::ProtocolError));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_reboot_bootloader_sends_communication_error_if_reboot_listener_dropped(
    ) -> Result<()> {
        let (reboot_client, _) = create_endpoints::<RebootListenerMarker>()?;
        let (_, proxy) = setup(vec![Reply::Okay("".to_string())]).await;
        let res = proxy.reboot_bootloader(reboot_client).await?;
        assert_eq!(res.err(), Some(FastbootError::CommunicationError));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    // Disabling until we can make this not rely on a timeout
    #[ignore]
    async fn test_reboot_bootloader_sends_rediscovered_error_if_not_rediscovered() -> Result<()> {
        let (reboot_client, reboot_server) = create_endpoints::<RebootListenerMarker>()?;
        let mut stream = reboot_server.into_stream()?;
        let (_, proxy) = setup(vec![Reply::Okay("".to_string())]).await;
        try_join!(
            async move {
                // Should only need to wait for the first request.
                if let Some(RebootListenerRequest::OnReboot { control_handle: _ }) =
                    stream.try_next().await?
                {
                    // Don't push rediscovered event
                    return Ok(());
                }
                bail!("did not receive reboot event");
            },
            proxy
                .reboot_bootloader(reboot_client)
                .map_err(|e| anyhow!("error rebooting to bootloader: {:?}", e)),
        )
        .and_then(|(_, reboot)| {
            assert_eq!(reboot.err(), Some(FastbootError::RediscoveredError));
            Ok(())
        })
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_continue_boot() -> Result<()> {
        let (_, proxy) = setup(vec![Reply::Okay("".to_string())]).await;
        proxy.continue_boot().await?.map_err(|e| anyhow!("error continue boot: {:?}", e))
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_continue_boot_sends_protocol_error_after_unexpected_reply() -> Result<()> {
        let (_, proxy) = setup(vec![Reply::Fail("".to_string())]).await;
        let res = proxy.continue_boot().await?;
        assert_eq!(res.err(), Some(FastbootError::ProtocolError));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_set_active() -> Result<()> {
        let (_, proxy) = setup(vec![Reply::Okay("".to_string())]).await;
        proxy.set_active("a").await?.map_err(|e| anyhow!("error set active: {:?}", e))
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_set_active_sends_protocol_error_after_unexpected_reply() -> Result<()> {
        let (_, proxy) = setup(vec![Reply::Fail("".to_string())]).await;
        let res = proxy.set_active("a").await?;
        assert_eq!(res.err(), Some(FastbootError::ProtocolError));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_stage() -> Result<()> {
        let (prog_client, prog_server) = create_endpoints::<UploadProgressListenerMarker>()?;
        let mut stream = prog_server.into_stream()?;
        let file: NamedTempFile = NamedTempFile::new().expect("tmp access failed");
        let mut buffer = BufWriter::new(&file);
        buffer.write_all(b"Test")?;
        buffer.flush()?;
        let (_, proxy) = setup(vec![
            Reply::Data(4),
            Reply::Okay("".to_string()), //Download Reply
        ])
        .await;
        let filepath = file.path().to_str().ok_or(anyhow!("error getting tempfile path"))?;
        try_join!(
            async move {
                while let Some(_) = stream.try_next().await? { /* do nothing */ }
                Ok(())
            },
            proxy.stage(filepath, prog_client).map_err(|e| anyhow!("error staging: {:?}", e)),
        )
        .and_then(|(_, stage)| {
            assert!(stage.is_ok());
            Ok(())
        })
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_stage_sends_protocol_error_after_unexpected_reply() -> Result<()> {
        let (prog_client, prog_server) = create_endpoints::<UploadProgressListenerMarker>()?;
        let mut stream = prog_server.into_stream()?;
        let file: NamedTempFile = NamedTempFile::new().expect("tmp access failed");
        let mut buffer = BufWriter::new(&file);
        buffer.write_all(b"Test")?;
        buffer.flush()?;
        let (_, proxy) = setup(vec![Reply::Data(6)]).await;
        let filepath = file.path().to_str().ok_or(anyhow!("error getting tempfile path"))?;
        try_join!(
            async move {
                while let Some(_) = stream.try_next().await? { /* do nothing */ }
                Ok(())
            },
            proxy.stage(filepath, prog_client).map_err(|e| anyhow!("error staging: {:?}", e)),
        )
        .and_then(|(_, stage)| {
            assert_eq!(stage.err(), Some(FastbootError::ProtocolError));
            Ok(())
        })
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_oem() -> Result<()> {
        let (_, proxy) = setup(vec![Reply::Okay("".to_string())]).await;
        proxy.oem("a").await?.map_err(|e| anyhow!("error oem: {:?}", e))
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_oem_sends_protocol_error_after_unexpected_reply() -> Result<()> {
        let (_, proxy) = setup(vec![Reply::Fail("".to_string())]).await;
        let res = proxy.oem("a").await?;
        assert_eq!(res.err(), Some(FastbootError::ProtocolError));
        Ok(())
    }
}
