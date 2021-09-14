// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context as _, Result},
    ffx_core::TryStreamUtilExt,
    ffx_daemon_events::TargetEvent,
    ffx_daemon_target::target::Target,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_developer_bridge as bridge,
    fuchsia_async::futures::TryStreamExt,
    services::Context,
    std::future::Future,
    std::pin::Pin,
    std::rc::Rc,
};

// TODO(awdavies): Abstract this to use similar utilities to an actual service.
// This functionally behaves the same with the only caveat being that some
// initial state is set by the caller (the target Rc).
#[derive(Debug)]
pub(crate) struct TargetHandle {}

impl TargetHandle {
    pub(crate) fn new(
        target: Rc<Target>,
        cx: Context,
        handle: ServerEnd<bridge::TargetHandleMarker>,
    ) -> Result<Pin<Box<dyn Future<Output = ()>>>> {
        let inner = TargetHandleInner { target };
        let stream = handle.into_stream()?;
        let fut = Box::pin(async move {
            let _ = stream
                .map_err(|err| anyhow!("{}", err))
                .try_for_each_concurrent_while_connected(None, |req| inner.handle(&cx, req))
                .await;
        });
        Ok(fut)
    }
}

struct TargetHandleInner {
    target: Rc<Target>,
}

impl TargetHandleInner {
    async fn handle(&self, _cx: &Context, req: bridge::TargetHandleRequest) -> Result<()> {
        match req {
            bridge::TargetHandleRequest::GetSshAddress { responder } => {
                // Product state and manual state are the two states where an
                // address is guaranteed. If the target is not in that state,
                // then wait for its state to change.
                let connection_state = self.target.get_connection_state();
                if !connection_state.is_product() && !connection_state.is_manual() {
                    self.target
                        .events
                        .wait_for(None, |e| {
                            if let TargetEvent::ConnectionStateChanged(_, state) = e {
                                // It's not clear if it is possible to change
                                // the state to `Manual`, but check for it just
                                // in case.
                                state.is_product() || state.is_manual()
                            } else {
                                false
                            }
                        })
                        .await
                        .context("waiting for connection state changes")?;
                }
                // After the event fires it should be guaranteed that the
                // SSH address is written to the target.
                let poll_duration = std::time::Duration::from_millis(15);
                loop {
                    if let Some(mut addr) = self.target.ssh_address_info() {
                        return responder.send(&mut addr).map_err(Into::into);
                    }
                    fuchsia_async::Timer::new(poll_duration).await;
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use chrono::Utc;
    use ffx_daemon_events::TargetConnectionState;
    use ffx_daemon_target::target::{TargetAddrEntry, TargetAddrType};
    use fuchsia_async::Task;
    use services::testing::FakeDaemonBuilder;
    use std::net::{IpAddr, SocketAddr};
    use std::str::FromStr;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_valid_target_state() {
        const TEST_SOCKETADDR: &'static str = "[fe80::1%1]:22";
        let daemon = FakeDaemonBuilder::new().build();
        let cx = Context::new(daemon);
        let target = Target::new_with_addr_entries(
            Some("pride-and-prejudice"),
            vec![TargetAddrEntry::new(
                SocketAddr::from_str(TEST_SOCKETADDR).unwrap().into(),
                Utc::now(),
                TargetAddrType::Ssh,
            )]
            .into_iter(),
        );
        target.update_connection_state(|_| TargetConnectionState::Mdns(std::time::Instant::now()));
        let (proxy, server) =
            fidl::endpoints::create_proxy::<bridge::TargetHandleMarker>().unwrap();
        let _handle = Task::local(TargetHandle::new(target, cx, server).unwrap());
        let result = proxy.get_ssh_address().await.unwrap();
        if let bridge::TargetAddrInfo::IpPort(bridge::TargetIpPort {
            ip: fidl_fuchsia_net::IpAddress::Ipv6(fidl_fuchsia_net::Ipv6Address { addr }),
            ..
        }) = result
        {
            assert_eq!(IpAddr::from(addr), SocketAddr::from_str(TEST_SOCKETADDR).unwrap().ip());
        } else {
            panic!("incorrect address received: {:?}", result);
        }
    }
}
