// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Result},
    async_trait::async_trait,
    ffx_daemon_target::{fastboot::find_devices, FASTBOOT_CHECK_INTERVAL},
    ffx_stream_util::TryStreamUtilExt,
    fidl::endpoints::ProtocolMarker,
    fidl_fuchsia_developer_bridge as bridge,
    fuchsia_async::Task,
    futures::TryStreamExt,
    protocols::prelude::*,
    std::rc::Rc,
};

struct Inner {
    events_in: async_channel::Receiver<bridge::FastbootTarget>,
    events_out: async_channel::Sender<bridge::FastbootTarget>,
}

#[ffx_protocol]
#[derive(Default)]
pub struct FastbootTargetStreamProtocol {
    inner: Option<Rc<Inner>>,
    fastboot_task: Option<Task<()>>,
}

#[async_trait(?Send)]
impl FidlProtocol for FastbootTargetStreamProtocol {
    type Protocol = bridge::FastbootTargetStreamMarker;
    type StreamHandler = FidlStreamHandler<Self>;

    async fn handle(&self, _cx: &Context, req: bridge::FastbootTargetStreamRequest) -> Result<()> {
        match req {
            bridge::FastbootTargetStreamRequest::GetNext { responder } => responder
                .send(
                    self.inner
                        .as_ref()
                        .expect("inner state should have been initialized")
                        .events_in
                        .recv()
                        .await?,
                )
                .map_err(Into::into),
        }
    }

    async fn start(&mut self, _cx: &Context) -> Result<()> {
        let (sender, receiver) = async_channel::bounded::<bridge::FastbootTarget>(1);
        let inner = Rc::new(Inner { events_in: receiver, events_out: sender });
        self.inner.replace(inner.clone());
        let inner = Rc::downgrade(&inner);
        self.fastboot_task.replace(Task::local(async move {
            loop {
                let fastboot_devices = find_devices().await;
                if let Some(inner) = inner.upgrade() {
                    for dev in fastboot_devices {
                        let _ = inner
                            .events_out
                            .send(bridge::FastbootTarget {
                                serial: Some(dev.serial),
                                ..bridge::FastbootTarget::EMPTY
                            })
                            .await;
                    }
                } else {
                    break;
                }
                fuchsia_async::Timer::new(FASTBOOT_CHECK_INTERVAL).await;
            }
        }));
        Ok(())
    }

    async fn stop(&mut self, _cx: &Context) -> Result<()> {
        self.fastboot_task
            .take()
            .ok_or(anyhow!("fuchsia_target_stream task never started"))?
            .cancel()
            .await;
        Ok(())
    }

    async fn serve<'a>(
        &'a self,
        cx: &'a Context,
        stream: <Self::Protocol as ProtocolMarker>::RequestStream,
    ) -> Result<()> {
        stream
            .map_err(|err| anyhow!("{}", err))
            .try_for_each_concurrent_while_connected(None, |req| self.handle(cx, req))
            .await
    }
}
