// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_ledger_cloud_test::{
    CloudControllerFactoryRequest, CloudControllerFactoryRequestStream, CloudControllerRequest,
    CloudControllerRequestStream, NetworkState,
};
use futures::future::LocalFutureObj;
use futures::prelude::*;
use futures::select;
use futures::stream::FuturesUnordered;
use std::cell::RefCell;
use std::rc::Rc;

use crate::filter;
use crate::session::{CloudSession, CloudSessionFuture, CloudSessionShared};
use crate::state::Cloud;

fn filter_for_network_state(s: NetworkState) -> Box<dyn filter::RequestFilter> {
    match s {
        NetworkState::Connected => Box::new(filter::Always::new(filter::Status::Ok)),
        NetworkState::Disconnected => Box::new(filter::Always::new(filter::Status::NetworkError)),
        NetworkState::InjectNetworkErrors => Box::new(filter::Flaky::new(2)),
    }
}

pub struct CloudControllerFactory {
    storage: Rc<RefCell<Cloud>>,
    controllers: FuturesUnordered<CloudControllerFuture>,
    requests: stream::Fuse<CloudControllerFactoryRequestStream>,
    rng: Rc<RefCell<dyn rand::RngCore>>,
}

type CloudControllerFactoryFuture = LocalFutureObj<'static, ()>;

impl CloudControllerFactory {
    pub fn new(
        requests: CloudControllerFactoryRequestStream,
        rng: Rc<RefCell<dyn rand::RngCore>>,
    ) -> CloudControllerFactory {
        CloudControllerFactory {
            storage: Rc::new(RefCell::new(Cloud::new())),
            controllers: FuturesUnordered::new(),
            requests: requests.fuse(),
            rng,
        }
    }

    async fn handle_requests(mut self) -> Result<(), fidl::Error> {
        loop {
            select! {
                _ = self.controllers.next() => {
                    // One of the controller futures completed. We don't need to do anything.
                },
                req = self.requests.try_next() =>
                    match req? {
                        None => return Ok(()),
                        Some(CloudControllerFactoryRequest::Build {controller, ..}) => {
                            let controller = controller.into_stream()?;
                            self.controllers.push(
                                CloudController::new(
                                    self.storage.clone(),
                                    self.rng.clone(),
                                    controller,
                                ).run()
                            )
                        }
                    }
            }
        }
    }

    pub fn run(self) -> CloudControllerFactoryFuture {
        LocalFutureObj::new(Box::new(self.handle_requests().map(|_| ())))
    }
}

struct CloudController {
    cloud_futures: FuturesUnordered<CloudSessionFuture>,
    controller_requests: stream::Fuse<CloudControllerRequestStream>,
    cloud_state: Rc<CloudSessionShared>,
}

type CloudControllerFuture = LocalFutureObj<'static, ()>;

impl CloudController {
    fn new(
        storage: Rc<RefCell<Cloud>>,
        rng: Rc<RefCell<dyn rand::RngCore>>,
        controller: CloudControllerRequestStream,
    ) -> CloudController {
        let state = Rc::new(CloudSessionShared::new(storage, rng));
        CloudController {
            controller_requests: controller.fuse(),
            cloud_state: state,
            cloud_futures: FuturesUnordered::new(),
        }
    }

    async fn handle_requests(mut self) -> Result<(), fidl::Error> {
        loop {
            select! {
                _ = self.cloud_futures.next() => {
                    // One of the cloud provider futures completed. We
                    // don't need to do anything.
                },
                req = self.controller_requests.try_next() =>
                    match req? {
                        None => return Ok(()),
                        Some(CloudControllerRequest::SetNetworkState {state, responder}) => {
                            self.cloud_state.set_filter(filter_for_network_state(state));
                            responder.send()?
                        },
                        Some(CloudControllerRequest::Connect {provider, ..}) => {
                            let provider = provider.into_stream()?;
                            self.cloud_futures.push(CloudSession::new(Rc::clone(&self.cloud_state), provider).run())
                        },
                        Some(CloudControllerRequest::SetDiffSupport {support, responder}) => {
                            *self.cloud_state.diff_support.borrow_mut() = support;
                            responder.send()?
                        }
                    }
            }
        }
    }

    fn run(self) -> CloudControllerFuture {
        LocalFutureObj::new(Box::new(self.handle_requests().map(|_| ())))
    }
}

#[cfg(test)]
mod tests {
    use fidl::endpoints::create_endpoints;
    use fidl_fuchsia_ledger_cloud::{
        CloudProviderMarker, CloudProviderProxy, DeviceSetMarker, DeviceSetProxy, Status,
    };
    use fidl_fuchsia_ledger_cloud_test::{
        CloudControllerFactoryMarker, CloudControllerFactoryProxy, CloudControllerMarker,
        CloudControllerProxy, NetworkState,
    };
    use fuchsia_async as fasync;
    use pin_utils::pin_mut;

    use super::*;

    struct DeviceSetConnection {
        cloud_controller: CloudControllerProxy,
        #[allow(unused)]
        cloud_provider: CloudProviderProxy,
        device_set: DeviceSetProxy,
    }

    fn rng() -> Rc<RefCell<dyn rand::RngCore>> {
        Rc::new(RefCell::new(rand::thread_rng()))
    }

    async fn connect_device_set(
        factory: &CloudControllerFactoryProxy,
    ) -> Result<DeviceSetConnection, fidl::Error> {
        let (cloud_controller, cloud_controller_request) =
            create_endpoints::<CloudControllerMarker>()?;
        factory.build(cloud_controller_request)?;
        let cloud_controller = cloud_controller.into_proxy()?;

        let (cloud_provider, cloud_provider_request) = create_endpoints::<CloudProviderMarker>()?;
        cloud_controller.connect(cloud_provider_request)?;
        let cloud_provider = cloud_provider.into_proxy()?;

        let (device_set, device_set_request) = create_endpoints::<DeviceSetMarker>()?;
        assert_eq!(cloud_provider.get_device_set(device_set_request).await?, Status::Ok);
        let device_set = device_set.into_proxy()?;

        Ok(DeviceSetConnection { cloud_controller, cloud_provider, device_set })
    }

    #[test]
    /// Checks that instances from a cloud controller factory share
    /// their cloud state.
    fn cloud_controller_factory_cloud_shared() {
        let mut exec = fasync::Executor::new().unwrap();

        let (client, server) = create_endpoints::<CloudControllerFactoryMarker>().unwrap();

        let stream = server.into_stream().unwrap();
        let server_fut = CloudControllerFactory::new(stream, rng()).run();
        fasync::spawn_local(server_fut);

        let proxy = client.into_proxy().unwrap();
        let client_fut = async move {
            let client0 = connect_device_set(&proxy).await.unwrap();
            let client1 = connect_device_set(&proxy).await.unwrap();
            let fingerprint = || vec![1, 2, 3].into_iter();

            assert_eq!(
                client0.device_set.set_fingerprint(&mut fingerprint()).await.unwrap(),
                Status::Ok
            );
            assert_eq!(
                client1.device_set.check_fingerprint(&mut fingerprint()).await.unwrap(),
                Status::Ok
            );
        };
        pin_mut!(client_fut);
        assert!(exec.run_until_stalled(&mut client_fut).is_ready());
    }

    #[test]
    /// Checks that instances from a cloud controller factory do not
    /// share their network state.
    fn cloud_controller_factory_network_unshared() {
        let mut exec = fasync::Executor::new().unwrap();

        let (client, server) = create_endpoints::<CloudControllerFactoryMarker>().unwrap();

        let stream = server.into_stream().unwrap();
        let server_fut = CloudControllerFactory::new(stream, rng()).run();
        fasync::spawn_local(server_fut);

        let proxy = client.into_proxy().unwrap();
        let client_fut = async move {
            let client0 = connect_device_set(&proxy).await.unwrap();
            let client1 = connect_device_set(&proxy).await.unwrap();
            let fingerprint = || vec![1, 2, 3].into_iter();

            // Disconnect the first provider.
            client0.cloud_controller.set_network_state(NetworkState::Disconnected).await.unwrap();
            assert_eq!(
                client0.device_set.check_fingerprint(&mut fingerprint()).await.unwrap(),
                Status::NetworkError
            );
            assert_eq!(
                client1.device_set.check_fingerprint(&mut fingerprint()).await.unwrap(),
                Status::NotFound
            );
        };
        pin_mut!(client_fut);
        assert!(exec.run_until_stalled(&mut client_fut).is_ready());
    }
}
