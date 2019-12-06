// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_net_policy::{ObserverRequest, ObserverRequestStream},
    fidl_fuchsia_net_stack::StackProxy,
    fuchsia_syslog::fx_log_err,
    fuchsia_zircon as zx,
    futures::lock::Mutex,
    futures::prelude::*,
    std::collections::HashMap,
    std::sync::Arc,
};

pub async fn serve_fidl_requests(
    stack: StackProxy,
    stream: ObserverRequestStream,
    _interface_ids: Arc<Mutex<HashMap<String, u64>>>,
) -> Result<(), fidl::Error> {
    stream
        .try_for_each(|req| {
            async {
                match req {
                    ObserverRequest::ListInterfaces { responder } => {
                        let mut ifaces = stack.list_interfaces().await?;
                        responder.send(&mut ifaces.iter_mut(), zx::sys::ZX_OK)
                    }
                    ObserverRequest::GetInterfaceInfo { name, responder } => {
                        let mut ifaces = stack.list_interfaces().await?;
                        ifaces.retain(|i| i.properties.name == name);
                        let (mut result, status) = if ifaces.len() == 0 {
                            (None, zx::sys::ZX_ERR_NOT_FOUND)
                        } else if ifaces.len() == 1 {
                            (ifaces.pop(), zx::sys::ZX_OK)
                        } else {
                            fx_log_err!("{} nics have the same name {}", ifaces.len(), name);
                            (None, zx::sys::ZX_ERR_INTERNAL)
                        };
                        responder.send(result.as_mut(), status)
                    }
                }
            }
        })
        .await
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        fidl::endpoints::create_proxy,
        fidl_fuchsia_net_policy::{ObserverMarker, ObserverProxy},
        fidl_fuchsia_net_stack::{
            AdministrativeStatus, InterfaceAddress, InterfaceInfo, InterfaceProperties,
            PhysicalStatus, StackMarker, StackRequest, StackRequestStream,
        },
        fuchsia_async as fasync,
        futures::task::Poll,
        pin_utils::pin_mut,
        std::boxed::Box,
    };

    const ID1: u64 = 1;
    const ID2: u64 = 2;
    const NAME1: &str = "lo";
    const NAME2: &str = "eth";

    fn build_interface_properties(name: String) -> InterfaceProperties {
        InterfaceProperties {
            name: name,
            topopath: "/all/the/way/home".to_owned(),
            filepath: "/dev/class/ethernet/123".to_owned(),
            mac: Some(Box::new(fidl_fuchsia_hardware_ethernet::MacAddress {
                octets: [0, 1, 2, 255, 254, 253],
            })),
            mtu: 1500,
            features: 2,
            administrative_status: AdministrativeStatus::Enabled,
            physical_status: PhysicalStatus::Up,
            addresses: vec![InterfaceAddress {
                ip_address: fidl_fuchsia_net::IpAddress::Ipv4(fidl_fuchsia_net::Ipv4Address {
                    addr: [1, 1, 1, 1],
                }),
                prefix_len: 4,
            }],
        }
    }

    fn build_interface_info(id: u64, name: String) -> InterfaceInfo {
        InterfaceInfo { id: id, properties: build_interface_properties(name) }
    }

    fn setup_interface_tests() -> (fasync::Executor, impl Future, ObserverProxy, StackRequestStream)
    {
        let exec = fasync::Executor::new().expect("failed to create an executor");

        // Set up mock stack fidl server.
        let (stack_proxy, stack_server) =
            create_proxy::<StackMarker>().expect("failed to create stack fidl");
        let stack_stream =
            stack_server.into_stream().expect("failed to create a stack request stream.");

        // Set up real Observer fidl server and client.
        let (observer_proxy, observer_server) =
            create_proxy::<ObserverMarker>().expect("failed to create observer fidl");
        let observer_stream =
            observer_server.into_stream().expect("failed to create an observer request stream.");

        // Create and initialize interface_ids
        let mut interface_ids = HashMap::new();
        interface_ids.insert(NAME1.to_owned(), ID1);
        interface_ids.insert(NAME2.to_owned(), ID2);
        let interface_ids = Arc::new(Mutex::new(interface_ids));

        let observer_service_task =
            serve_fidl_requests(stack_proxy, observer_stream, interface_ids)
                .unwrap_or_else(|e| fx_log_err!("failed to serve observer FIDL call: {}", e));
        (exec, observer_service_task, observer_proxy, stack_stream)
    }

    #[test]
    fn list_interfaces_test() {
        let (mut exec, observer_service_task, observer_proxy, mut stack_stream) =
            setup_interface_tests();

        // Call observer FIDL call.
        let client_fut = observer_proxy.list_interfaces();

        // Let observer client run to stall.
        pin_mut!(client_fut);
        assert!(exec.run_until_stalled(&mut client_fut).is_pending());

        // Let observer server run to stall.
        pin_mut!(observer_service_task);
        assert!(exec.run_until_stalled(&mut observer_service_task).is_pending());

        // Let stack server run to stall and check that we got an appropriate FIDL call.
        let event = match exec.run_until_stalled(&mut stack_stream.next()) {
            Poll::Ready(Some(Ok(req))) => req,
            _ => panic!("Expected a stack fidl call, but there is none!"),
        };

        match event {
            StackRequest::ListInterfaces { responder } => {
                responder
                    .send(&mut vec![build_interface_info(ID2, NAME2.to_string())].iter_mut())
                    .expect("failed to send list interface response");
            }
            _ => panic!("Unexpected stack call!"),
        };

        assert!(exec.run_until_stalled(&mut observer_service_task).is_pending());

        // Let observer client run until ready, and check that we got an expected response.
        let (infos, status) = match exec.run_until_stalled(&mut client_fut) {
            Poll::Ready(Ok(req)) => req,
            other => panic!("Expected a response from observer fidl call, but got {:?}!", other),
        };
        assert_eq!(zx::sys::ZX_OK, status);
        assert_eq!(vec![build_interface_info(ID2, NAME2.to_string())], infos);
    }

    #[test]
    fn get_interface_info_test() {
        let (mut exec, observer_service_task, observer_proxy, mut stack_stream) =
            setup_interface_tests();

        // Call observer FIDL call.
        let client_fut = observer_proxy.get_interface_info(NAME2);

        // Let observer client run to stall.
        pin_mut!(client_fut);
        assert!(exec.run_until_stalled(&mut client_fut).is_pending());

        // Let observer server run to stall.
        pin_mut!(observer_service_task);
        assert!(exec.run_until_stalled(&mut observer_service_task).is_pending());

        // Let stack server run to stall and check that we got an appropriate FIDL call.
        let event = match exec.run_until_stalled(&mut stack_stream.next()) {
            Poll::Ready(Some(Ok(req))) => req,
            _ => panic!("Expected a stack fidl call, but there is none!"),
        };

        match event {
            StackRequest::ListInterfaces { responder } => {
                responder
                    .send(&mut vec![build_interface_info(ID2, NAME2.to_string())].iter_mut())
                    .expect("failed to send list interface response");
            }
            _ => panic!("Unexpected stack call!"),
        };

        assert!(exec.run_until_stalled(&mut observer_service_task).is_pending());

        // Let observer client run until ready, and check that we got an expected response.
        let (response, status) = match exec.run_until_stalled(&mut client_fut) {
            Poll::Ready(Ok(req)) => req,
            other => panic!("Expected a response from observer fidl call, but got {:?}", other),
        };
        assert_eq!(zx::sys::ZX_OK, status);
        assert_eq!(build_interface_info(ID2, NAME2.to_string()), *response.unwrap());
    }

    #[test]
    fn get_interface_info_not_found_test() {
        let (mut exec, observer_service_task, observer_proxy, mut stack_stream) =
            setup_interface_tests();

        // Call observer FIDL call.
        let client_fut = observer_proxy.get_interface_info("unknown");

        // Let observer client run to stall.
        pin_mut!(client_fut);
        assert!(exec.run_until_stalled(&mut client_fut).is_pending());

        // Let observer server run to stall.
        pin_mut!(observer_service_task);
        assert!(exec.run_until_stalled(&mut observer_service_task).is_pending());

        // Let stack server run to stall and check that we got an appropriate FIDL call.
        let event = match exec.run_until_stalled(&mut stack_stream.next()) {
            Poll::Ready(Some(Ok(req))) => req,
            _ => panic!("Expected a stack fidl call, but there is none!"),
        };
        match event {
            StackRequest::ListInterfaces { responder } => {
                responder
                    .send(&mut vec![build_interface_info(ID2, NAME2.to_string())].iter_mut())
                    .expect("failed to send list interface response");
            }
            _ => panic!("Unexpected stack call: {:?}", event),
        };
        assert!(exec.run_until_stalled(&mut observer_service_task).is_pending());

        // Let observer client run to stall and check that we got an expected response.
        let (_response, status) = match exec.run_until_stalled(&mut client_fut) {
            Poll::Ready(Ok(req)) => req,
            other => panic!("Expected a response from observer fidl call, but got {:?}", other),
        };

        assert_eq!(zx::Status::NOT_FOUND.into_raw(), status);
    }
}
