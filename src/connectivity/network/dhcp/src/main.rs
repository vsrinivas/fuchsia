// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

use {
    anyhow::{Context as _, Error},
    argh::FromArgs,
    dhcp::{
        configuration,
        protocol::{Message, SERVER_PORT},
        server::{Server, ServerAction, ServerDispatcher, DEFAULT_STASH_ID, DEFAULT_STASH_PREFIX},
    },
    fuchsia_async::{self as fasync, net::UdpSocket, Interval},
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon::DurationNum,
    futures::{Future, FutureExt, StreamExt, TryFutureExt, TryStreamExt},
    net2::unix::UnixUdpBuilderExt,
    std::{
        cell::RefCell,
        net::{IpAddr, Ipv4Addr},
        os::unix::io::AsRawFd,
    },
    void::Void,
};

/// A buffer size in excess of the maximum allowable DHCP message size.
const BUF_SZ: usize = 1024;
const DEFAULT_CONFIG_PATH: &str = "/pkg/data/config.json";
/// The rate in seconds at which expiration DHCP leases are recycled back into the managed address
/// pool. The current value of 5 is meant to facilitate manual testing.
// TODO(atait): Replace with Duration type after it has been updated to const fn.
const EXPIRATION_INTERVAL_SECS: i64 = 5;

enum IncomingService {
    Server(fidl_fuchsia_net_dhcp::Server_RequestStream),
}

/// The Fuchsia DHCP server.
#[derive(Debug, FromArgs)]
#[argh(name = "dhcpd")]
pub struct Args {
    /// the identifier used to access fuchsia.stash.Store. dhcpd will attempt to access its
    /// configuration parameters, saved DHCP option values, and saved leases from the
    /// fuchsia.stash.Store instance specified by this identifier. If there are no configuration
    /// parameters etc. at the specified fuchsia.stash.Store instance, then dhcpd will fallback to
    /// parameters stored in the default configuration file.
    #[argh(option, default = "DEFAULT_STASH_ID.to_string()")]
    pub stash: String,

    /// the path to the default configuration file consumed by dhcpd if it was unable to access a
    /// fuchsia.stash.Store instance.
    #[argh(option, default = "DEFAULT_CONFIG_PATH.to_string()")]
    pub config: String,
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["dhcpd"])?;

    let Args { config, stash } = argh::from_env();
    let stash = dhcp::stash::Stash::new(&stash, DEFAULT_STASH_PREFIX)
        .context("failed to instantiate stash")?;
    let default_params = configuration::load_server_params_from_file(&config)
        .context("failed to load default server parameters from configuration file")?;
    let params = stash.load_parameters().await.unwrap_or_else(|e| {
        log::warn!("failed to load parameters from stash: {:?}", e);
        default_params.clone()
    });
    let socks = if params.bound_device_names.len() > 0 {
        params.bound_device_names.iter().map(String::as_str).try_fold::<_, _, Result<_, Error>>(
            Vec::new(),
            |mut acc, name| {
                let sock = create_socket(Some(name))?;
                let () = acc.push(sock);
                Ok(acc)
            },
        )?
    } else {
        vec![create_socket(None)?]
    };
    if socks.len() == 0 {
        return Err(anyhow::Error::msg("no valid sockets to receive messages from"));
    }
    let options = stash.load_options().await.unwrap_or_else(|e| {
        log::warn!("failed to load options from stash: {:?}", e);
        std::collections::HashMap::new()
    });
    let cache = stash.load_client_configs().await.unwrap_or_else(|e| {
        log::warn!("failed to load cached client config from stash: {:?}", e);
        std::collections::HashMap::new()
    });
    let server = RefCell::new(Server::new(stash, params, options, cache));

    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(IncomingService::Server);
    fs.take_and_serve_directory_handle()?;
    let admin_fut =
        fs.then(futures::future::ok).try_for_each_concurrent(None, |incoming_service| async {
            match incoming_service {
                IncomingService::Server(stream) => {
                    run_server(stream, &server, &default_params)
                        .inspect_err(|e| log::warn!("run_server failed: {:?}", e))
                        .await?;
                    Ok(())
                }
            }
        });

    if !server.borrow().is_serving() {
        log::info!("starting server in configuration only mode");
        let () = admin_fut.await?;
    } else {
        let msg_loops = socks
            .into_iter()
            .map(|sock| define_msg_handling_loop_future(sock, &server).boxed_local());
        let lease_expiration_handler = define_lease_expiration_handler_future(&server);
        log::info!("starting server");
        let (_void, (), ()) = futures::try_join!(
            futures::future::select_ok(msg_loops),
            admin_fut,
            lease_expiration_handler
        )?;
    }
    Ok(())
}

fn create_socket(name: Option<&str>) -> Result<UdpSocket, Error> {
    let sock = net2::UdpBuilder::new_v4()?;
    // Since dhcpd may listen to multiple interfaces, we must enable
    // SO_REUSEPORT so that binding the same (address, port) pair to each
    // interface can still succeed.
    let sock = sock.reuse_port(true)?;
    if let Some(name) = name {
        // There are currently no safe Rust interfaces to set SO_BINDTODEVICE,
        // so we must set it through libc.
        if unsafe {
            libc::setsockopt(
                sock.as_raw_fd(),
                libc::SOL_SOCKET,
                libc::SO_BINDTODEVICE,
                name.as_ptr() as *const libc::c_void,
                name.len() as libc::socklen_t,
            )
        } == -1
        {
            return Err(anyhow::format_err!(
                "setsockopt(SO_BINDTODEVICE) failed for {}: {}",
                name,
                std::io::Error::last_os_error()
            ));
        }
    }
    let sock = sock.bind((Ipv4Addr::UNSPECIFIED, SERVER_PORT))?;
    let () = sock.set_broadcast(true)?;
    Ok(UdpSocket::from_socket(sock)?)
}

async fn define_msg_handling_loop_future(
    sock: UdpSocket,
    server: &RefCell<Server>,
) -> Result<Void, Error> {
    let mut buf = vec![0u8; BUF_SZ];
    loop {
        let (received, mut sender) =
            sock.recv_from(&mut buf).await.context("failed to read from socket")?;
        log::info!("received message from: {}", sender);
        let msg = Message::from_buffer(&buf[..received])?;
        log::info!("parsed message: {:?}", msg);

        // This call should not block because the server is single-threaded.
        let result = server.borrow_mut().dispatch(msg);
        match result {
            Err(e) => log::error!("error processing client message: {:?}", e),
            Ok(ServerAction::AddressRelease(addr)) => log::info!("released address: {}", addr),
            Ok(ServerAction::AddressDecline(addr)) => log::info!("allocated address: {}", addr),
            Ok(ServerAction::SendResponse(message, dest)) => {
                log::info!("generated response: {:?}", message);

                // Check if server returned an explicit destination ip.
                if let Some(addr) = dest {
                    sender.set_ip(IpAddr::V4(addr));
                }

                let response_buffer = message.serialize();
                sock.send_to(&response_buffer, sender).await.context("unable to send response")?;
                log::info!("response sent to: {}", sender);
            }
        }
    }
}

fn define_lease_expiration_handler_future<'a>(
    server: &'a RefCell<Server>,
) -> impl Future<Output = Result<(), Error>> + 'a {
    let expiration_interval = Interval::new(EXPIRATION_INTERVAL_SECS.seconds());
    expiration_interval
        .map(move |()| server.borrow_mut().release_expired_leases())
        .map(|_| Ok(()))
        .try_collect::<()>()
}

async fn run_server<S: ServerDispatcher>(
    stream: fidl_fuchsia_net_dhcp::Server_RequestStream,
    server: &RefCell<S>,
    default_params: &dhcp::configuration::ServerParameters,
) -> Result<(), fidl::Error> {
    stream
        .try_for_each(|request| async {
            match request {
                fidl_fuchsia_net_dhcp::Server_Request::GetOption { code: c, responder: r } => {
                    r.send(&mut server.borrow().dispatch_get_option(c).map_err(|e| e.into_raw()))
                }
                fidl_fuchsia_net_dhcp::Server_Request::GetParameter { name: n, responder: r } => {
                    r.send(&mut server.borrow().dispatch_get_parameter(n).map_err(|e| e.into_raw()))
                }
                fidl_fuchsia_net_dhcp::Server_Request::SetOption { value: v, responder: r } => r
                    .send(
                        &mut server.borrow_mut().dispatch_set_option(v).map_err(|e| e.into_raw()),
                    ),
                fidl_fuchsia_net_dhcp::Server_Request::SetParameter { value: v, responder: r } => r
                    .send(
                        &mut server
                            .borrow_mut()
                            .dispatch_set_parameter(v)
                            .map_err(|e| e.into_raw()),
                    ),
                fidl_fuchsia_net_dhcp::Server_Request::ListOptions { responder: r } => {
                    r.send(&mut server.borrow().dispatch_list_options().map_err(|e| e.into_raw()))
                }
                fidl_fuchsia_net_dhcp::Server_Request::ListParameters { responder: r } => r.send(
                    &mut server.borrow().dispatch_list_parameters().map_err(|e| e.into_raw()),
                ),
                fidl_fuchsia_net_dhcp::Server_Request::ResetOptions { responder: r } => r.send(
                    &mut server.borrow_mut().dispatch_reset_options().map_err(|e| e.into_raw()),
                ),
                fidl_fuchsia_net_dhcp::Server_Request::ResetParameters { responder: r } => r.send(
                    &mut server
                        .borrow_mut()
                        .dispatch_reset_parameters(&default_params)
                        .map_err(|e| e.into_raw()),
                ),
                fidl_fuchsia_net_dhcp::Server_Request::ClearLeases { responder: r } => r.send(
                    &mut server
                        .borrow_mut()
                        .dispatch_clear_leases()
                        .map_err(fuchsia_zircon::Status::into_raw),
                ),
            }
        })
        .await
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::convert::TryFrom;

    struct CannedDispatcher {}

    impl ServerDispatcher for CannedDispatcher {
        fn dispatch_get_option(
            &self,
            _code: fidl_fuchsia_net_dhcp::OptionCode,
        ) -> Result<fidl_fuchsia_net_dhcp::Option_, fuchsia_zircon::Status> {
            Ok(fidl_fuchsia_net_dhcp::Option_::SubnetMask(fidl_fuchsia_net::Ipv4Address {
                addr: [0, 0, 0, 0],
            }))
        }
        fn dispatch_get_parameter(
            &self,
            _name: fidl_fuchsia_net_dhcp::ParameterName,
        ) -> Result<fidl_fuchsia_net_dhcp::Parameter, fuchsia_zircon::Status> {
            Ok(fidl_fuchsia_net_dhcp::Parameter::Lease(fidl_fuchsia_net_dhcp::LeaseLength {
                default: None,
                max: None,
            }))
        }
        fn dispatch_set_option(
            &mut self,
            _value: fidl_fuchsia_net_dhcp::Option_,
        ) -> Result<(), fuchsia_zircon::Status> {
            Ok(())
        }
        fn dispatch_set_parameter(
            &mut self,
            _value: fidl_fuchsia_net_dhcp::Parameter,
        ) -> Result<(), fuchsia_zircon::Status> {
            Ok(())
        }
        fn dispatch_list_options(
            &self,
        ) -> Result<Vec<fidl_fuchsia_net_dhcp::Option_>, fuchsia_zircon::Status> {
            Ok(vec![])
        }
        fn dispatch_list_parameters(
            &self,
        ) -> Result<Vec<fidl_fuchsia_net_dhcp::Parameter>, fuchsia_zircon::Status> {
            Ok(vec![])
        }
        fn dispatch_reset_options(&mut self) -> Result<(), fuchsia_zircon::Status> {
            Ok(())
        }
        fn dispatch_reset_parameters(
            &mut self,
            _defaults: &dhcp::configuration::ServerParameters,
        ) -> Result<(), fuchsia_zircon::Status> {
            Ok(())
        }
        fn dispatch_clear_leases(&mut self) -> Result<(), fuchsia_zircon::Status> {
            Ok(())
        }
    }

    fn default_params() -> dhcp::configuration::ServerParameters {
        dhcp::configuration::ServerParameters {
            server_ips: vec![Ipv4Addr::from([192, 168, 0, 1])],
            lease_length: dhcp::configuration::LeaseLength {
                default_seconds: 86400,
                max_seconds: 86400,
            },
            managed_addrs: dhcp::configuration::ManagedAddresses {
                network_id: Ipv4Addr::from([192, 168, 0, 0]),
                broadcast: Ipv4Addr::from([192, 168, 0, 128]),
                mask: dhcp::configuration::SubnetMask::try_from(25).unwrap(),
                pool_range_start: Ipv4Addr::from([192, 168, 0, 0]),
                pool_range_stop: Ipv4Addr::from([192, 168, 0, 0]),
            },
            permitted_macs: dhcp::configuration::PermittedMacs(vec![]),
            static_assignments: dhcp::configuration::StaticAssignments(
                std::collections::HashMap::new(),
            ),
            arp_probe: false,
            bound_device_names: vec![],
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn get_option_with_subnet_mask_returns_subnet_mask() -> Result<(), Error> {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_net_dhcp::Server_Marker>()?;
        let server = RefCell::new(CannedDispatcher {});

        let defaults = default_params();
        let res = futures::select! {
            res = proxy.get_option(fidl_fuchsia_net_dhcp::OptionCode::SubnetMask).fuse() => res.context("get_option failed"),
            server_fut = run_server(stream, &server, &defaults).fuse() => Err(anyhow::Error::msg("server finished before request")),
        }?;

        let expected_result =
            Ok(fidl_fuchsia_net_dhcp::Option_::SubnetMask(fidl_fuchsia_net::Ipv4Address {
                addr: [0, 0, 0, 0],
            }));
        assert_eq!(res, expected_result);
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn get_parameter_with_lease_length_returns_lease_length() -> Result<(), Error> {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_net_dhcp::Server_Marker>()?;
        let server = RefCell::new(CannedDispatcher {});

        let defaults = default_params();
        let res = futures::select! {
            res = proxy.get_parameter(fidl_fuchsia_net_dhcp::ParameterName::LeaseLength).fuse() => res.context("get_parameter failed"),
            server_fut = run_server(stream, &server, &defaults).fuse() => Err(anyhow::Error::msg("server finished before request")),
        }?;
        let expected_result =
            Ok(fidl_fuchsia_net_dhcp::Parameter::Lease(fidl_fuchsia_net_dhcp::LeaseLength {
                default: None,
                max: None,
            }));
        assert_eq!(res, expected_result);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn set_option_with_subnet_mask_returns_unit() -> Result<(), Error> {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_net_dhcp::Server_Marker>()?;
        let server = RefCell::new(CannedDispatcher {});

        let defaults = default_params();
        let res = futures::select! {
            res = proxy.set_option(&mut fidl_fuchsia_net_dhcp::Option_::SubnetMask(
            fidl_fuchsia_net::Ipv4Address { addr: [0, 0, 0, 0] },
        )).fuse() => res.context("set_option failed"),
            server_fut = run_server(stream, &server, &defaults).fuse() => Err(anyhow::Error::msg("server finished before request")),
        }?;
        assert_eq!(res, Ok(()));
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn set_parameter_with_lease_length_returns_unit() -> Result<(), Error> {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_net_dhcp::Server_Marker>()?;
        let server = RefCell::new(CannedDispatcher {});

        let defaults = default_params();
        let res = futures::select! {
            res = proxy.set_parameter(&mut fidl_fuchsia_net_dhcp::Parameter::Lease(
            fidl_fuchsia_net_dhcp::LeaseLength { default: None, max: None },
        )).fuse() => res.context("set_parameter failed"),
            server_fut = run_server(stream, &server, &defaults).fuse() => Err(anyhow::Error::msg("server finished before request")),
        }?;
        assert_eq!(res, Ok(()));
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn list_options_returns_empty_vec() -> Result<(), Error> {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_net_dhcp::Server_Marker>()?;
        let server = RefCell::new(CannedDispatcher {});

        let defaults = default_params();
        let res = futures::select! {
            res = proxy.list_options().fuse() => res.context("list_options failed"),
            server_fut = run_server(stream, &server, &defaults).fuse() => Err(anyhow::Error::msg("server finished before request")),
        }?;
        assert_eq!(res, Ok(vec![]));
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn list_parameters_returns_empty_vec() -> Result<(), Error> {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_net_dhcp::Server_Marker>()?;
        let server = RefCell::new(CannedDispatcher {});

        let defaults = default_params();
        let res = futures::select! {
            res = proxy.list_parameters().fuse() => res.context("list_parameters failed"),
            server_fut = run_server(stream, &server, &defaults).fuse() => Err(anyhow::Error::msg("server finished before request")),
        }?;
        assert_eq!(res, Ok(vec![]));
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn reset_options_returns_unit() -> Result<(), Error> {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_net_dhcp::Server_Marker>()?;
        let server = RefCell::new(CannedDispatcher {});

        let defaults = default_params();
        let res = futures::select! {
            res = proxy.reset_options().fuse() => res.context("reset_options failed"),
            server_fut = run_server(stream, &server, &defaults).fuse() => Err(anyhow::Error::msg("server finished before request")),
        }?;

        assert_eq!(res, Ok(()));
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn reset_parameters_returns_unit() -> Result<(), Error> {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_net_dhcp::Server_Marker>()?;
        let server = RefCell::new(CannedDispatcher {});

        let defaults = default_params();
        let res = futures::select! {
            res = proxy.reset_parameters().fuse() => res.context("reset_parameters failed"),
            server_fut = run_server(stream, &server, &defaults).fuse() => Err(anyhow::Error::msg("server finished before request")),
        }?;

        assert_eq!(res, Ok(()));
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn clear_leases_returns_unit() -> Result<(), Error> {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_net_dhcp::Server_Marker>()?;
        let server = RefCell::new(CannedDispatcher {});

        let defaults = default_params();
        let res = futures::select! {
            res = proxy.clear_leases().fuse() => res.context("clear_leases failed"),
            server_fut = run_server(stream, &server, &defaults).fuse() => Err(anyhow::Error::msg("server finished before request")),
        }?;

        assert_eq!(res, Ok(()));
        Ok(())
    }
}
