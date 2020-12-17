// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(not(target_os = "fuchsia"))]
mod not_fuchsia;
#[cfg(not(target_os = "fuchsia"))]
pub use not_fuchsia::{hard_coded_security_context, Hoist, HostOvernet, DEFAULT_ASCENDD_PATH};

#[cfg(target_os = "fuchsia")]
mod fuchsia;
#[cfg(target_os = "fuchsia")]
pub use fuchsia::Hoist;

use anyhow::Error;
use fidl_fuchsia_overnet::{MeshControllerProxy, ServiceConsumerProxy, ServicePublisherProxy};

#[cfg(target_os = "fuchsia")]
pub mod logger {
    pub fn init() -> Result<(), anyhow::Error> {
        use anyhow::Context as _;
        fuchsia_syslog::init_with_tags(&["overnet_hoist"]).context("initialize logging")?;
        Ok(())
    }
}

pub trait OvernetInstance: Sync + Send {
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

lazy_static::lazy_static! {
    static ref HOIST: Hoist = Hoist::new().unwrap();
}

pub fn hoist() -> &'static Hoist {
    &HOIST
}

#[cfg(test)]
mod test {

    use super::*;
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

    #[cfg(not(target_os = "fuchsia"))]
    fn init_ascendd() -> ascendd_lib::Ascendd {
        let n: u128 = rand::random();
        let ascendd_path = format!("/tmp/ascendd-for-hoist-test.{}.sock", n);
        std::env::set_var("ASCENDD", &ascendd_path);
        ascendd_lib::Ascendd::new(
            ascendd_lib::Opt { sockpath: Some(ascendd_path), ..Default::default() },
            Box::new(async_std::io::stdout()),
        )
        .unwrap()
    }

    #[cfg(not(target_os = "fuchsia"))]
    lazy_static::lazy_static! {
        static ref ASCENDD: ascendd_lib::Ascendd = init_ascendd();
    }

    fn init() {
        #[cfg(not(target_os = "fuchsia"))]
        lazy_static::initialize(&ASCENDD);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn one_bad_channel_doesnt_take_everything_down() {
        init();
        let (tx_complete, mut rx_complete) = oneshot::channel();
        let (tx_complete_ack, rx_complete_ack) = oneshot::channel();
        let service_consumer1 = hoist().connect_as_service_consumer().unwrap();
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
        let service_consumer2 = hoist().connect_as_service_consumer().unwrap();
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

    #[fuchsia_async::run_singlethreaded(test)]
    async fn one_bad_link_doesnt_take_the_rest_down() {
        init();
        let mesh_controller = &hoist().connect_as_mesh_controller().unwrap();
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
