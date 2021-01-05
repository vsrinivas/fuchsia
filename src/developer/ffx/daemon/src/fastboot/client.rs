// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::fastboot::{
        continue_boot, erase, flash, oem, reboot, reboot_bootloader, set_active, stage,
    },
    crate::target::{Target, TargetEvent},
    anyhow::{bail, Context, Result},
    async_trait::async_trait,
    fidl_fuchsia_developer_bridge::{FastbootError, FastbootRequest, FastbootRequestStream},
    futures::prelude::*,
    futures::try_join,
    std::io::{Read, Write},
    std::time::Duration,
    usb_bulk::Interface,
};

pub(crate) struct Fastboot(pub(crate) FastbootImpl<Interface>);

impl Fastboot {
    pub(crate) fn new(target: Target) -> Self {
        Self(FastbootImpl::new(target, Box::new(UsbFactory {})))
    }
}

#[async_trait]
pub(crate) trait InterfaceFactory<T: Read + Write + Send> {
    async fn interface(&self, target: &Target) -> Result<T>;
}

struct UsbFactory {}

#[async_trait]
impl InterfaceFactory<Interface> for UsbFactory {
    async fn interface(&self, target: &Target) -> Result<Interface> {
        match target.usb().await {
            Some(u) => Ok(u),
            None => bail!("Could not open usb interface for target: {:?}", target),
        }
    }
}

pub(crate) struct FastbootImpl<T: Read + Write + Send> {
    pub(crate) target: Target,
    pub(crate) usb: Option<T>,
    pub(crate) usb_factory: Box<dyn InterfaceFactory<T> + Send + Sync>,
}

impl<T: Read + Write + Send> FastbootImpl<T> {
    pub(crate) fn new(
        target: Target,
        usb_factory: Box<dyn InterfaceFactory<T> + Send + Sync>,
    ) -> Self {
        Self { target, usb: None, usb_factory }
    }

    fn clear_usb(&mut self) {
        self.usb = None
    }

    async fn usb(&mut self) -> Result<&mut T> {
        if let None = self.usb {
            self.usb = Some(self.usb_factory.interface(&self.target).await?);
        }
        Ok(self.usb.as_mut().expect("usb interface not available"))
    }

    pub(crate) async fn handle_fastboot_requests_from_stream(
        &mut self,
        mut stream: FastbootRequestStream,
    ) -> Result<()> {
        while let Some(req) = stream.try_next().await? {
            self.handle_fastboot_request(req).await?;
        }
        Ok(())
    }

    async fn handle_fastboot_request(&mut self, req: FastbootRequest) -> Result<()> {
        log::debug!("fastboot - received req: {:?}", req);
        let usb = self.usb().await?;
        match req {
            FastbootRequest::Flash { partition_name, path, responder } => {
                match flash(usb, &path, &partition_name) {
                    Ok(_) => responder.send(&mut Ok(()))?,
                    Err(e) => {
                        log::error!(
                            "Error flashing \"{}\" from {}:\n{:?}",
                            partition_name,
                            path,
                            e
                        );
                        responder
                            .send(&mut Err(FastbootError::ProtocolError))
                            .context("sending error response")?;
                    }
                }
            }
            FastbootRequest::Erase { partition_name, responder } => {
                match erase(usb, &partition_name) {
                    Ok(_) => responder.send(&mut Ok(()))?,
                    Err(e) => {
                        log::error!("Error erasing \"{}\": {:?}", partition_name, e);
                        responder
                            .send(&mut Err(FastbootError::ProtocolError))
                            .context("sending error response")?;
                    }
                }
            }
            FastbootRequest::Reboot { responder } => match reboot(usb) {
                Ok(_) => responder.send(&mut Ok(()))?,
                Err(e) => {
                    log::error!("Error rebooting: {:?}", e);
                    responder
                        .send(&mut Err(FastbootError::ProtocolError))
                        .context("sending error response")?;
                }
            },
            FastbootRequest::RebootBootloader { listener, responder } => {
                match reboot_bootloader(usb) {
                    Ok(_) => {
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
                                    .map_err(|e| FastbootError::CommunicationError)?
                                    .on_reboot()
                                    .map_err(|e| FastbootError::CommunicationError)
                            }
                        ) {
                            Ok(_) => {
                                log::debug!("Rediscovered reboot target");
                                self.clear_usb();
                                responder.send(&mut Ok(()))?;
                            }
                            Err(e) => {
                                log::error!("Error rebooting and rediscovering target: {:?}", e);
                                // Clear the usb connection just to be sure.
                                self.clear_usb();
                                responder.send(&mut Err(e)).context("sending error response")?;
                                return Ok(());
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
            FastbootRequest::ContinueBoot { responder } => match continue_boot(usb) {
                Ok(_) => responder.send(&mut Ok(()))?,
                Err(e) => {
                    log::error!("Error continuing boot: {:?}", e);
                    responder
                        .send(&mut Err(FastbootError::ProtocolError))
                        .context("sending error response")?;
                }
            },
            FastbootRequest::SetActive { slot, responder } => match set_active(usb, &slot) {
                Ok(_) => responder.send(&mut Ok(()))?,
                Err(e) => {
                    log::error!("Error setting active: {:?}", e);
                    responder
                        .send(&mut Err(FastbootError::ProtocolError))
                        .context("sending error response")?;
                }
            },
            FastbootRequest::Stage { path, responder } => match stage(usb, &path) {
                Ok(_) => responder.send(&mut Ok(()))?,
                Err(e) => {
                    log::error!("Error setting active: {:?}", e);
                    responder
                        .send(&mut Err(FastbootError::ProtocolError))
                        .context("sending error response")?;
                }
            },
            FastbootRequest::Oem { command, responder } => match oem(usb, &command) {
                Ok(_) => responder.send(&mut Ok(()))?,
                Err(e) => {
                    log::error!("Error sending oem \"{}\": {:?}", command, e);
                    responder
                        .send(&mut Err(FastbootError::ProtocolError))
                        .context("sending error response")?;
                }
            },
        }
        Ok(())
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use {
        super::*,
        crate::onet::create_ascendd,
        anyhow::anyhow,
        fastboot::reply::Reply,
        fidl::endpoints::{create_endpoints, create_proxy_and_stream},
        fidl_fuchsia_developer_bridge::{
            FastbootError, FastbootMarker, FastbootProxy, RebootListenerMarker,
            RebootListenerRequest,
        },
        std::io::BufWriter,
        std::sync::Arc,
        tempfile::NamedTempFile,
    };

    struct TestTransport {
        replies: Vec<Reply>,
    }

    impl Read for TestTransport {
        fn read(&mut self, buf: &mut [u8]) -> std::result::Result<usize, std::io::Error> {
            match self.replies.pop() {
                Some(r) => {
                    let reply = Vec::<u8>::from(r);
                    buf[..reply.len()].copy_from_slice(&reply);
                    Ok(reply.len())
                }
                None => Ok(0),
            }
        }
    }

    impl Write for TestTransport {
        fn write(&mut self, buf: &[u8]) -> std::result::Result<usize, std::io::Error> {
            Ok(buf.len())
        }

        fn flush(&mut self) -> std::result::Result<(), std::io::Error> {
            unimplemented!()
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

    #[async_trait]
    impl InterfaceFactory<TestTransport> for TestFactory {
        async fn interface(&self, _target: &Target) -> Result<TestTransport> {
            let mut transport = TestTransport::new();
            self.replies.iter().rev().for_each(|r| transport.push(r.clone()));
            return Ok(transport);
        }
    }

    async fn setup(replies: Vec<Reply>) -> (Target, FastbootProxy) {
        let ascendd = Arc::new(create_ascendd().await.unwrap());
        let target = Target::new(ascendd, "scooby-dooby-doo");
        let mut fb =
            FastbootImpl::<TestTransport>::new(target.clone(), Box::new(TestFactory::new(replies)));
        let (proxy, stream) = create_proxy_and_stream::<FastbootMarker>().unwrap();
        fuchsia_async::Task::spawn(async move {
            match fb.handle_fastboot_requests_from_stream(stream).await {
                Ok(_) => log::debug!("Fastboot proxy finished - client disconnected"),
                Err(e) => log::error!("There was an error handling fastboot requests: {:?}", e),
            }
        })
        .detach();
        (target, proxy)
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_flash() -> Result<()> {
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
        proxy.flash("test", filepath).await?.map_err(|e| anyhow!("error flashing: {:?}", e))
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_flash_sends_protocol_error_after_unexpected_reply() -> Result<()> {
        let file: NamedTempFile = NamedTempFile::new().expect("tmp access failed");
        let mut buffer = BufWriter::new(&file);
        buffer.write_all(b"Test")?;
        buffer.flush()?;
        let (_, proxy) = setup(vec![Reply::Data(6)]).await;
        let filepath = file.path().to_str().ok_or(anyhow!("error getting tempfile path"))?;
        let res = proxy.flash("test", filepath).await?;
        assert_eq!(res.err(), Some(FastbootError::ProtocolError));
        Ok(())
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
                    return target.events.push(TargetEvent::Rediscovered).await;
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
        proxy.stage(filepath).await?.map_err(|e| anyhow!("error staging: {:?}", e))
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_stage_sends_protocol_error_after_unexpected_reply() -> Result<()> {
        let file: NamedTempFile = NamedTempFile::new().expect("tmp access failed");
        let mut buffer = BufWriter::new(&file);
        buffer.write_all(b"Test")?;
        buffer.flush()?;
        let (_, proxy) = setup(vec![Reply::Data(6)]).await;
        let filepath = file.path().to_str().ok_or(anyhow!("error getting tempfile path"))?;
        let res = proxy.stage(filepath).await?;
        assert_eq!(res.err(), Some(FastbootError::ProtocolError));
        Ok(())
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
