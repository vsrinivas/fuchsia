// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::fastboot::{
        continue_boot, erase, flash, get_var, oem, reboot, reboot_bootloader, set_active, stage,
        UploadProgressListener,
    },
    crate::target::{ConnectionState, Target, TargetEvent},
    anyhow::{anyhow, bail, Context, Result},
    async_once::Once,
    async_trait::async_trait,
    fastboot::UploadProgressListener as _,
    fidl::Error as FidlError,
    fidl_fuchsia_developer_bridge::{
        FastbootError, FastbootRequest, FastbootRequestStream, RebootListenerProxy,
    },
    fidl_fuchsia_developer_remotecontrol::RemoteControlProxy,
    fidl_fuchsia_hardware_power_statecontrol::{AdminMarker, AdminProxy},
    futures::io::{AsyncRead, AsyncWrite},
    futures::prelude::*,
    futures::try_join,
    std::time::Duration,
};

const ADMIN_SELECTOR: &str = "core/appmgr:out:fuchsia.hardware.power.statecontrol.Admin";

#[async_trait(?Send)]
pub(crate) trait InterfaceFactory<T: AsyncRead + AsyncWrite + Unpin> {
    async fn open(&mut self, target: &Target) -> Result<T>;
    async fn close(&self);
}

pub(crate) struct FastbootImpl<T: AsyncRead + AsyncWrite + Unpin> {
    pub(crate) target: Target,
    pub(crate) usb: Option<T>,
    pub(crate) usb_factory: Box<dyn InterfaceFactory<T>>,
    remote_proxy: Once<RemoteControlProxy>,
    admin_proxy: Once<AdminProxy>,
}

impl<T: AsyncRead + AsyncWrite + Unpin> FastbootImpl<T> {
    pub(crate) fn new(target: Target, usb_factory: Box<dyn InterfaceFactory<T>>) -> Self {
        Self { target, usb: None, usb_factory, remote_proxy: Once::new(), admin_proxy: Once::new() }
    }

    async fn clear_usb(&mut self) {
        self.usb = None;
        self.usb_factory.close().await;
    }

    async fn usb(&mut self) -> Result<&mut T> {
        if self.usb.is_none() {
            self.usb.replace(self.usb_factory.open(&self.target).await?);
        }
        Ok(self.usb.as_mut().expect("usb interface not available"))
    }

    pub(crate) async fn handle_fastboot_requests_from_stream(
        &mut self,
        mut stream: FastbootRequestStream,
    ) -> Result<()> {
        while let Some(req) = stream.try_next().await? {
            match self.handle_fastboot_request(req).await {
                Ok(_) => (),
                Err(e) => {
                    self.clear_usb().await;
                    return Err(e);
                }
            }
        }
        // Make sure the serial is no longer in use.
        self.clear_usb().await;
        Ok(())
    }

    async fn prepare_device(&self, listener: &RebootListenerProxy) -> Result<()> {
        match self.target.get_connection_state() {
            ConnectionState::Fastboot(_) => Ok(()),
            _ => {
                listener.on_reboot()?;
                match try_join!(
                    async {
                        match self.get_admin_proxy().await?.reboot_to_bootloader().await {
                            Ok(_) => Ok(()),
                            Err(_e @ FidlError::ClientChannelClosed { .. }) => Ok(()),
                            Err(e) => bail!(e),
                        }
                    },
                    self.target.events.wait_for(Some(Duration::from_secs(10)), |e| {
                        e == TargetEvent::Rediscovered
                    })
                ) {
                    Ok(_) => match self.target.get_connection_state() {
                        ConnectionState::Fastboot(_) => Ok(()),
                        _ => bail!("Could not reboot device to fastboot - state does not match"),
                    },
                    Err(e) => Err(e),
                }
            }
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
                match get_var(self.usb().await?, &name).await {
                    Ok(value) => responder.send(&mut Ok(value))?,
                    Err(e) => {
                        log::error!("Error getting variable '{}': {:?}", name, e);
                        responder
                            .send(&mut Err(FastbootError::ProtocolError))
                            .context("sending error response")?;
                    }
                }
            }
            FastbootRequest::Flash { partition_name, path, listener, responder } => {
                let upload_listener = UploadProgressListener::new(listener)?;
                match flash(self.usb().await?, &path, &partition_name, &upload_listener).await {
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
                match erase(self.usb().await?, &partition_name).await {
                    Ok(_) => responder.send(&mut Ok(()))?,
                    Err(e) => {
                        log::error!("Error erasing \"{}\": {:?}", partition_name, e);
                        responder
                            .send(&mut Err(FastbootError::ProtocolError))
                            .context("sending error response")?;
                    }
                }
            }
            FastbootRequest::Reboot { responder } => match reboot(self.usb().await?).await {
                Ok(_) => responder.send(&mut Ok(()))?,
                Err(e) => {
                    log::error!("Error rebooting: {:?}", e);
                    responder
                        .send(&mut Err(FastbootError::ProtocolError))
                        .context("sending error response")?;
                }
            },
            FastbootRequest::RebootBootloader { listener, responder } => {
                match reboot_bootloader(self.usb().await?).await {
                    Ok(_) => {
                        self.clear_usb().await;
                        match try_join!(
                            self.target
                                .events
                                .wait_for(Some(Duration::from_secs(10)), |e| {
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
                                responder.send(&mut Err(e)).context("sending error response")?;
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
                match continue_boot(self.usb().await?).await {
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
                match set_active(self.usb().await?, &slot).await {
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
                match stage(self.usb().await?, &path, &UploadProgressListener::new(listener)?).await
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
                match oem(self.usb().await?, &command).await {
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

    async fn setup(replies: Vec<Reply>) -> (Target, FastbootProxy) {
        ffx_config::init_config_test().unwrap();

        let target = Target::new("scooby-dooby-doo");
        let mut fb =
            FastbootImpl::<TestTransport>::new(target.clone(), Box::new(TestFactory::new(replies)));
        let (proxy, stream) = create_proxy_and_stream::<FastbootMarker>().unwrap();
        fuchsia_async::Task::local(async move {
            match fb.handle_fastboot_requests_from_stream(stream).await {
                Ok(_) => log::debug!("Fastboot proxy finished - client disconnected"),
                Err(e) => log::error!("There was an error handling fastboot requests: {:?}", e),
            }
            assert!(fb.usb.is_none());
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
