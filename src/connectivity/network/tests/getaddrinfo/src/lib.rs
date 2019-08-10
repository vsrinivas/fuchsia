// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

#[cfg(test)]
mod test {
    use {
        fidl::endpoints::{DiscoverableService, ServiceMarker},
        fidl_fuchsia_net as fnet, fidl_fuchsia_sys as fsys, fuchsia_async as fasync,
        fuchsia_component::client::connect_to_service,
        fuchsia_component::server::ServiceFs,
        fuchsia_zircon as zx,
        futures::StreamExt,
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_getaddrinfo() {
        let (client_chan, server_chan) = zx::Channel::create().unwrap();
        let (controller, server_end) =
            fidl::endpoints::create_proxy::<fsys::ComponentControllerMarker>().unwrap();

        let mut launch_info = fsys::LaunchInfo {
            url: "fuchsia-pkg://fuchsia.com/getaddrinfo_tests#meta/getaddrinfo_test_client.cmx"
                .to_string(),
            arguments: None,
            out: None,
            err: None,
            directory_request: None,
            flat_namespace: None,
            additional_services: Some(Box::new(fsys::ServiceList {
                names: vec![fnet::NameLookupMarker::SERVICE_NAME.to_string()],
                provider: None,
                host_directory: Some(client_chan),
            })),
        };
        let launcher_svc = connect_to_service::<fsys::LauncherMarker>().unwrap();
        launcher_svc.create_component(&mut launch_info, Some(server_end)).unwrap();

        let mut fs = ServiceFs::new();
        fs.add_fidl_service_at(
            fnet::NameLookupMarker::NAME,
            |mut stream: fnet::NameLookupRequestStream| {
                fasync::spawn(async move {
                    while let Some(Ok(fnet::NameLookupRequest::LookupIp {
                        hostname,
                        options,
                        responder,
                    })) = stream.next().await
                    {
                        let mut result = fnet::IpAddressInfo {
                            ipv4_addrs: vec![],
                            ipv6_addrs: vec![],
                            canonical_name: None,
                        };
                        if hostname == "example.com" {
                            if options.contains(fnet::LookupIpOptions::V4Addrs) {
                                result.ipv4_addrs =
                                    vec![fnet::Ipv4Address { addr: [192, 0, 2, 1] }];
                            }
                            if options.contains(fnet::LookupIpOptions::V6Addrs) {
                                result.ipv6_addrs = vec![fnet::Ipv6Address {
                                    addr: [
                                        0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
                                    ],
                                }];
                            }
                            responder.send(&mut Ok(result)).unwrap();
                        } else {
                            responder.send(&mut Err(fnet::LookupError::NotFound)).unwrap();
                        }
                    }
                });
            },
        );
        fs.serve_connection(server_chan).unwrap();
        fs.collect::<()>().await;
        let mut controller_stream = controller.take_event_stream();
        match controller_stream.next().await.unwrap().unwrap() {
            fsys::ComponentControllerEvent::OnTerminated {
                termination_reason: fsys::TerminationReason::Exited,
                return_code: 0,
            } => (),
            event => {
                panic!("Unexpected component controller event: {:?}", event);
            }
        }
    }
}
