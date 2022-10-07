// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Error};
use fidl_fuchsia_overnet::{MeshControllerProxy, ServiceConsumerProxy, ServicePublisherProxy};
use once_cell::sync::OnceCell;

mod fuchsia;
mod not_fuchsia;

#[cfg(target_os = "fuchsia")]
pub use crate::fuchsia::*;
#[cfg(not(target_os = "fuchsia"))]
pub use not_fuchsia::*;

#[cfg(target_os = "fuchsia")]
pub mod logger {
    pub fn init() -> Result<(), anyhow::Error> {
        diagnostics_log::init!(&["overnet_hoist"]);
        Ok(())
    }
}

pub trait OvernetInstance: std::fmt::Debug + Sync + Send {
    fn connect_as_service_consumer(&self) -> Result<ServiceConsumerProxy, Error>;
    fn connect_as_service_publisher(&self) -> Result<ServicePublisherProxy, Error>;
    fn connect_as_mesh_controller(&self) -> Result<MeshControllerProxy, Error>;

    /// Connect to Overnet as a ServicePublisher, and then publish a single service
    fn publish_service(
        &self,
        service_name: &str,
        provider: fidl::endpoints::ClientEnd<fidl_fuchsia_overnet::ServiceProviderMarker>,
    ) -> Result<(), anyhow::Error> {
        self.connect_as_service_publisher()?.publish_service(service_name, provider)?;
        Ok(())
    }
}

static HOIST: OnceCell<Hoist> = OnceCell::new();

pub fn hoist() -> &'static Hoist {
    if cfg!(target_os = "fuchsia") {
        // on fuchsia, we always have a global hoist to return.
        HOIST.get_or_init(|| Hoist::new().unwrap())
    } else {
        // otherwise, don't return it until something sets it up.
        HOIST.get().expect("Tried to get overnet hoist before it was initialized")
    }
}

/// On non-fuchsia OS', call this at the start of the program to enable the global hoist.
pub fn init_hoist() -> Result<&'static Hoist, Error> {
    let hoist = Hoist::new()?;
    init_hoist_with(hoist)
}

/// On non-fuchsia OS', call this at the start of the program to make the provided hoist global
pub fn init_hoist_with(hoist: Hoist) -> Result<&'static Hoist, Error> {
    HOIST
        .set(hoist.clone())
        .map_err(|_| anyhow::anyhow!("Tried to set global hoist more than once"))?;
    HOIST.get().context("Failed to retrieve the hoist we created back from the cell we put it in")
}

#[cfg(test)]
mod test {
    use super::*;
    use ::fuchsia as fuchsia_lib;
    use anyhow::Error;
    use fuchsia_async::{Task, TimeoutExt};
    use futures::channel::oneshot;
    use futures::future::{select, try_join, Either};
    use futures::prelude::*;
    use std::time::Duration;

    async fn loop_on_list_peers_until_it_fails(
        service_consumer: &impl fidl_fuchsia_overnet::ServiceConsumerProxyInterface,
    ) -> Result<(), Error> {
        loop {
            service_consumer.list_peers().await?;
        }
    }

    #[fuchsia_lib::test]
    async fn one_bad_channel_doesnt_take_everything_down() {
        let hoist = Hoist::new().unwrap();
        let (tx_complete, mut rx_complete) = oneshot::channel();
        let (tx_complete_ack, rx_complete_ack) = oneshot::channel();
        let service_consumer1 = hoist.connect_as_service_consumer().unwrap();
        // we have one service consumer that fulfills contract by just listening for peers
        let _bg = Task::spawn(async move {
            loop {
                match select(service_consumer1.list_peers().boxed(), &mut rx_complete).await {
                    Either::Left((r, _)) => {
                        r.expect("list_peers on channel fulfilling contract should not fail");
                    }
                    Either::Right(_) => {
                        tx_complete_ack.send(()).unwrap();
                        return;
                    }
                }
            }
        });
        // and in the main task we have a second one that breaks contract by doing two list_peers
        let service_consumer2 = hoist.connect_as_service_consumer().unwrap();
        try_join(
            loop_on_list_peers_until_it_fails(&service_consumer2),
            loop_on_list_peers_until_it_fails(&service_consumer2),
        )
        .await
        .expect_err("Multiple list_peers against the same channel should fail");
        // signal completion and await the response
        tx_complete.send(()).unwrap();
        rx_complete_ack.await.unwrap();
    }

    #[fuchsia_lib::test]
    async fn one_bad_link_doesnt_take_the_rest_down() {
        let hoist = Hoist::new().unwrap();
        let mesh_controller = &hoist.connect_as_mesh_controller().unwrap();
        let (s1a, s1b) = fidl::Socket::create(fidl::SocketOpts::STREAM).unwrap();
        let (s2a, s2b) = fidl::Socket::create(fidl::SocketOpts::STREAM).unwrap();
        mesh_controller.attach_socket_link(s1a).unwrap();
        mesh_controller.attach_socket_link(s2a).unwrap();
        let mut s1b = fidl::AsyncSocket::from_socket(s1b).unwrap();
        drop(s2b);
        let mut buf = [0u8; 10];
        async move {
            loop {
                match s1b.read(&mut buf).await {
                    Ok(0) => panic!("Should not see s1b closed"),
                    Ok(_) => (),
                    Err(e) => panic!("Should not see an error on s1b: {:?}", e),
                }
            }
        }
        .on_timeout(Duration::from_secs(2), || ())
        .await
    }
}
