// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

// Explicitly added due to conflict using custom_attribute and async_await above.
#[macro_use]
extern crate serde_derive;

mod opts;

use {
    crate::opts::Opt,
    connectivity_testing::wlan_service_util,
    failure::{bail, Error, ResultExt},
    fidl_fuchsia_net_oldhttp::{self as http, HttpServiceProxy},
    fidl_fuchsia_net_stack::{self as netstack, StackMarker, StackProxy},
    fidl_fuchsia_wlan_device_service::{DeviceServiceMarker, DeviceServiceProxy},
    fidl_fuchsia_wlan_sme as fidl_sme, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_syslog::{self as syslog, fx_log_info},
    fuchsia_zircon as zx,
    futures::io::{AllowStdIo, AsyncReadExt},
    std::collections::HashMap,
    std::process,
    std::{thread, time},
    structopt::StructOpt,
};

#[allow(dead_code)]
type WlanService = DeviceServiceProxy;

fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["wlan-smoke-test"]).expect("should not fail");

    let opt = Opt::from_args();
    fx_log_info!("{:?}", opt);

    // create objects to hold test objects and results
    let mut test_results: TestResults = Default::default();

    let mut test_pass = true;
    if let Err(e) = run_test(opt, &mut test_results) {
        test_pass = false;
        test_results.error_message = e.to_string();
    }

    report_results(&mut test_results);

    if !test_pass {
        process::exit(1);
    }

    Ok(())
}

fn run_test(opt: Opt, test_results: &mut TestResults) -> Result<(), Error> {
    let mut test_pass = false;
    let mut exec = fasync::Executor::new().context("error creating event loop")?;
    let wlan_svc =
        connect_to_service::<DeviceServiceMarker>().context("Failed to connect to wlan_service")?;
    test_results.connect_to_wlan_service = true;

    let http_svc = connect_to_service::<http::HttpServiceMarker>()?;
    test_results.connect_to_http_service = true;

    let network_svc = connect_to_service::<StackMarker>()?;
    test_results.connect_to_netstack_service = true;

    let fut = async {
        let wlan_iface_ids = wlan_service_util::get_iface_list(&wlan_svc)
            .await
            .context("wlan-smoke-test: failed to query wlanservice iface list")?;
        test_results.query_wlan_service_iface_list = true;

        if wlan_iface_ids.is_empty() {
            bail!("Did not find wlan interfaces");
        };
        test_results.wlan_discovered = true;
        // note: interface discovery is marked false at the time of failure
        test_results.interface_status = true;

        for iface in wlan_iface_ids {
            let sme_proxy = wlan_service_util::get_iface_sme_proxy(&wlan_svc, iface).await?;
            let status_response = match sme_proxy.status().await {
                Ok(status) => status,
                Err(_) => {
                    test_results.interface_status = false;
                    continue;
                }
            };

            let iface_object = WlanIface::new(sme_proxy, status_response);

            test_results.iface_objects.insert(iface, iface_object);
        }

        // now that we have interfaces...  let's try to use them!
        for (iface_id, wlan_iface) in test_results.iface_objects.iter_mut() {
            // first check if we can get scan results
            let scan_result = wlan_service_util::perform_scan(&wlan_iface.sme_proxy).await;
            match scan_result {
                Ok(results) => {
                    wlan_iface.scan_success = true;
                    for entry in results.into_iter() {
                        if entry.best_bss.ssid == opt.target_ssid.as_bytes().to_vec() {
                            wlan_iface.scan_found_target_ssid = true;
                        }
                    }
                }
                _ => println!("scan failed"),
            };

            let mut requires_disconnect = false;
            // first check if we are connected to the target network already
            if is_connect_to_target_network_needed(
                opt.stay_connected,
                &opt.target_ssid,
                &wlan_iface.initial_status,
            ) {
                let connect_result = wlan_service_util::connect_to_network(
                    &wlan_iface.sme_proxy,
                    opt.target_ssid.as_bytes().to_vec(),
                    opt.target_pwd.as_bytes().to_vec(),
                )
                .await;

                match connect_result {
                    Ok(true) => {
                        wlan_iface.connection_success = true;
                        requires_disconnect = true;
                    }
                    _ => continue,
                };
            } else {
                // connection already established, mark as successful
                wlan_iface.connection_success = true;
            }

            let mut dhcp_check_attempts = 0;

            while dhcp_check_attempts < 3 && !wlan_iface.dhcp_success {
                // check if there is a non-zero ip addr as a first check for dhcp success
                let ip_addrs =
                    match get_ip_addrs_for_wlan_iface(&wlan_svc, &network_svc, *iface_id).await {
                        Ok(result) => result,
                        Err(_) => continue,
                    };
                if check_dhcp_complete(&ip_addrs) {
                    wlan_iface.dhcp_success = true;
                } else {
                    // dhcp takes some time...  loop again to give it a chance
                    dhcp_check_attempts += 1;
                    thread::sleep(time::Duration::from_millis(4000));
                }
            }

            // after testing, check if we need to disconnect
            if requires_disconnect {
                match wlan_service_util::disconnect_from_network(&wlan_iface.sme_proxy).await {
                    Err(_) => wlan_iface.disconnect_success = false,
                    _ => wlan_iface.disconnect_success = true,
                };
            } else {
                wlan_iface.disconnect_success = true;
            }

            // if any of the checks failed, throw an error to indicate a part of
            // the test failure
            if wlan_iface.connection_success
                && wlan_iface.dhcp_success
                && wlan_iface.data_transfer
                && wlan_iface.disconnect_success
            {
                // note: failures are logged at the point of the failure,
                // simply checking here to return overall test status
                test_pass = true;
            } else {
                test_pass = false;
            }

            // TODO(NET-1095): add ping check to verify connectivity

            // TODO(NET-1095): add http get to verify data when we can specify this interface
        }

        // create url (TODO(NET-1095): add command line option)
        let url_string = "http://ovh.net/files/1Mb.dat";
        let url_request = create_url_request(url_string);

        // NOTE: this is intended to loop over each wlan iface. For now,
        // make a single request to make sure that mechanism works and we have not broken
        // connectivity with connection changes
        fetch_and_discard_url(http_svc, url_request).await?;
        test_results.base_data_transfer = true;

        Ok(())
    };
    exec.run_singlethreaded(fut)?;

    if !test_pass {
        bail!("Saw a failure on at least one interface");
    }

    Ok(())
}

// Object to hold overall test status
#[derive(Default, Serialize)]
struct TestResults {
    connect_to_wlan_service: bool,
    connect_to_http_service: bool,
    connect_to_netstack_service: bool,
    query_wlan_service_iface_list: bool,
    wlan_discovered: bool,
    interface_status: bool,
    base_data_transfer: bool,

    #[serde(flatten)]
    iface_objects: HashMap<u16, WlanIface>,

    error_message: String,
}

// Object to hold test specific status
#[derive(Serialize)]
struct WlanIface {
    #[serde(skip_serializing)]
    sme_proxy: fidl_sme::ClientSmeProxy,

    #[serde(skip_serializing)]
    initial_status: fidl_sme::ClientStatusResponse,

    scan_success: bool,

    scan_found_target_ssid: bool,

    connection_success: bool,

    disconnect_success: bool,

    dhcp_success: bool,

    data_transfer: bool,
}

impl WlanIface {
    pub fn new(
        sme_proxy: fidl_sme::ClientSmeProxy,
        status: fidl_sme::ClientStatusResponse,
    ) -> WlanIface {
        WlanIface {
            sme_proxy: sme_proxy,
            initial_status: status,
            scan_success: false,
            scan_found_target_ssid: false,
            connection_success: false,
            disconnect_success: false,
            dhcp_success: false,
            data_transfer: false,
        }
    }
}

fn report_results(test_results: &TestResults) {
    println!("{}", serde_json::to_string_pretty(&test_results).unwrap());
}

fn is_connect_to_target_network_needed<T: AsRef<[u8]>>(
    stay_connected: bool,
    target_ssid: T,
    status: &fidl_sme::ClientStatusResponse,
) -> bool {
    if !stay_connected {
        // doesn't matter if we are connected, we will force a reconnection
        return true;
    }
    // are we already connected?  if so, check the current ssid
    match status.connected_to {
        Some(ref bss) if bss.ssid.as_slice() == target_ssid.as_ref() => false,
        _ => true,
    }
}

fn create_url_request<T: Into<String>>(url_string: T) -> http::UrlRequest {
    http::UrlRequest {
        url: url_string.into(),
        method: String::from("GET"),
        headers: None,
        body: None,
        response_body_buffer_size: 0,
        auto_follow_redirects: true,
        cache_mode: http::CacheMode::Default,
        response_body_mode: http::ResponseBodyMode::Stream,
    }
}

async fn fetch_and_discard_url(
    http_service: HttpServiceProxy,
    mut url_request: http::UrlRequest,
) -> Result<(), Error> {
    // Create a UrlLoader instance
    let (s, p) = zx::Channel::create().context("failed to create zx channel")?;
    let proxy = fasync::Channel::from_channel(p).context("failed to make async channel")?;

    let loader_server = fidl::endpoints::ServerEnd::<http::UrlLoaderMarker>::new(s);
    http_service.create_url_loader(loader_server)?;

    let loader_proxy = http::UrlLoaderProxy::new(proxy);
    let response = loader_proxy.start(&mut url_request).await?;

    if let Some(e) = response.error {
        bail!("UrlLoaderProxy error - code:{} ({})", e.code, e.description.unwrap_or("".into()))
    }

    let socket = match response.body.map(|x| *x) {
        Some(http::UrlBody::Stream(s)) => fasync::Socket::from_socket(s)?,
        _ => return Err(Error::from(zx::Status::BAD_STATE)),
    };

    // discard the bytes
    let mut stdio_sink = AllowStdIo::new(::std::io::sink());
    let bytes_received = socket.copy_into(&mut stdio_sink).await?;
    fx_log_info!("Received {:?} bytes", bytes_received);

    Ok(())
}

async fn get_ip_addrs_for_wlan_iface<'a>(
    wlan_svc: &'a DeviceServiceProxy,
    network_svc: &'a StackProxy,
    wlan_iface_id: u16,
) -> Result<Vec<netstack::InterfaceAddress>, Error> {
    // temporary implementation for getting the ip addrs for a wlan iface.  A more robust
    // lookup will be designed and implemented in the future (TODO: <bug already filed?>)

    let mut iface_path = String::new();

    //first get info on the wlan iface
    let response = wlan_svc.list_ifaces().await?;
    for iface in response.ifaces {
        if wlan_iface_id == iface.iface_id {
            // trim off any leading '@'s
            iface_path = iface.path.trim_start_matches('@').to_string();
        }
    }

    //now, if we got a valid path, we can check the netstack iface info
    if iface_path.is_empty() {
        // could not find a path...  throw an error
        bail!("Could not find the path for iface {}", wlan_iface_id);
    }

    let mut net_iface_response = network_svc.list_interfaces().await?;

    let mut wlan_iface_ip_addrs = Vec::new();

    for net_iface in net_iface_response.iter_mut() {
        if net_iface.properties.topopath.is_empty() {
            continue;
        }
        // trim off any leading '@'s
        let net_path = net_iface.properties.topopath.trim_start_matches('@').to_string();
        if net_path.starts_with(&iface_path) {
            // now get the ip addrs
            wlan_iface_ip_addrs.append(&mut net_iface.properties.addresses);

            // Note: Until proper interface mappings between wlanstack and netstack,
            // we return all ip_addrs that match the device path to handle
            // multiple interfaces on a single device.
        }
    }

    Ok(wlan_iface_ip_addrs)
}

fn check_dhcp_complete(ip_addrs: &[netstack::InterfaceAddress]) -> bool {
    for ip_addr in ip_addrs {
        // for now, assume a valid address if we see anything that isn't a 0
        fx_log_info!("checking validity of ip address: {:?}", ip_addr.ip_address);
        match ip_addr.ip_address {
            fidl_fuchsia_net::IpAddress::Ipv4(address) => {
                for &a in address.addr.iter() {
                    if a != 0 as u8 {
                        return true;
                    }
                }
            }
            fidl_fuchsia_net::IpAddress::Ipv6(address) => {
                for &a in address.addr.iter() {
                    if a != 0 as u8 {
                        return true;
                    }
                }
            }
        };
    }
    return false;
}

#[cfg(test)]
mod tests {
    use {
        super::*, fidl_fuchsia_net::IpAddress::Ipv4 as IPv4,
        fidl_fuchsia_net::IpAddress::Ipv6 as IPv6, fidl_fuchsia_net::Ipv4Address as IPv4_address,
        fidl_fuchsia_net::Ipv6Address as IPv6_address,
    };

    // helper values for tests
    static TEST_IPV4_ADDR: [u8; 4] = [1; 4];
    static TEST_IPV4_ALL_ZEROS: [u8; 4] = [0; 4];
    static TEST_IPV6_ADDR: [u8; 16] = [0x1; 16];
    static TEST_IPV6_ALL_ZEROS: [u8; 16] = [0; 16];

    /// Test to verify a valid ipv4 addr is assigned to an interface.  In the current
    /// implementation, only empty vectors or all zeros are considered to be invalid or unset.
    #[test]
    fn test_single_ipv4_addr_valid() {
        let ipv4_addr = netstack::InterfaceAddress {
            ip_address: IPv4(IPv4_address { addr: TEST_IPV4_ADDR }),
            prefix_len: 0,
        };
        assert!(check_dhcp_complete(&[ipv4_addr]));
    }

    /// Test to verify a valid ipv6 addr is assigned to an interface.  In the current
    /// implementation, only empty vectors or all zeros are considered to be invalid or unset.
    #[test]
    fn test_single_ipv6_addr_pass_dhcp_check() {
        let ipv6_addr = netstack::InterfaceAddress {
            ip_address: IPv6(IPv6_address { addr: TEST_IPV6_ADDR }),
            prefix_len: 0,
        };
        assert!(check_dhcp_complete(&[ipv6_addr]));
    }

    /// IPv4 addresses that are all zeros are considered invalid and should return false when
    /// chacked.
    #[test]
    fn test_single_ipv4_addr_all_zeros_fail_dhcp_check() {
        let ipv4_addr = netstack::InterfaceAddress {
            ip_address: IPv4(IPv4_address { addr: TEST_IPV4_ALL_ZEROS }),
            prefix_len: 0,
        };
        assert_eq!(check_dhcp_complete(&[ipv4_addr]), false);
    }

    /// IPv6 addresses that are all zeros are considered invalid and should return false when
    /// checked.
    #[test]
    fn test_single_ipv6_addr_all_zeros_fail_dhcp_check() {
        let ipv6_addr = netstack::InterfaceAddress {
            ip_address: IPv6(IPv6_address { addr: TEST_IPV6_ALL_ZEROS }),
            prefix_len: 0,
        };
        assert_eq!(check_dhcp_complete(&[ipv6_addr]), false);
    }

    /// Verify that having an assigned IPv4 address with an unset IPv6 address still returns true.
    #[test]
    fn test_valid_ipv4_with_unset_ipv6_pass_dhcp_check() {
        let ipv4_addr = netstack::InterfaceAddress {
            ip_address: IPv4(IPv4_address { addr: TEST_IPV4_ADDR }),
            prefix_len: 0,
        };
        let ipv6_addr = netstack::InterfaceAddress {
            ip_address: IPv6(IPv6_address { addr: TEST_IPV6_ALL_ZEROS }),
            prefix_len: 0,
        };
        assert!(check_dhcp_complete(&[ipv4_addr, ipv6_addr]));
    }

    /// Verify that having an assigned IPv6 address with an unset IPv4 address still returns true.
    #[test]
    fn test_valid_ipv6_with_unset_ipv4_pass_dhcp_check() {
        let ipv4_addr = netstack::InterfaceAddress {
            ip_address: IPv4(IPv4_address { addr: TEST_IPV4_ALL_ZEROS }),
            prefix_len: 0,
        };
        let ipv6_addr = netstack::InterfaceAddress {
            ip_address: IPv6(IPv6_address { addr: TEST_IPV6_ADDR }),
            prefix_len: 0,
        };
        assert!(check_dhcp_complete(&[ipv4_addr, ipv6_addr]));
    }

    /// Verify that having assigned IPv4 and IPv6 addresses returns true for DHCP check.
    #[test]
    fn test_unset_ipv4_with_unset_ipv6_fail_dhcp_check() {
        let ipv4_addr = netstack::InterfaceAddress {
            ip_address: IPv4(IPv4_address { addr: TEST_IPV4_ALL_ZEROS }),
            prefix_len: 0,
        };
        let ipv6_addr = netstack::InterfaceAddress {
            ip_address: IPv6(IPv6_address { addr: TEST_IPV6_ALL_ZEROS }),
            prefix_len: 0,
        };
        assert_eq!(check_dhcp_complete(&[ipv4_addr, ipv6_addr]), false);
    }

    /// Verify that having unset IPv4 and IPv6 addresses returns false and fails the DHCP check.
    #[test]
    fn test_unset_ipv6_and_unset_ipv4_fail_dhcp_check() {
        let ipv4_addr = netstack::InterfaceAddress {
            ip_address: IPv4(IPv4_address { addr: TEST_IPV4_ALL_ZEROS }),
            prefix_len: 0,
        };
        let ipv6_addr = netstack::InterfaceAddress {
            ip_address: IPv6(IPv6_address { addr: TEST_IPV6_ALL_ZEROS }),
            prefix_len: 0,
        };
        assert_eq!(check_dhcp_complete(&[ipv4_addr, ipv6_addr]), false);
    }

    /// Verify that the DHCP check fails when the provided interface address vector is empty.
    #[test]
    fn test_empty_interface_addresses_fail_dhcp_check() {
        assert_eq!(check_dhcp_complete(&[]), false);
    }

    /// Test to verify a connection will be triggered for an SSID that is not already connected.
    /// This is called with stay connected true and a different target SSID.
    #[test]
    fn test_target_network_needs_connection_targets_different_network() {
        let stay_connected = true;
        let target_ssid = "target_ssid";
        let current_ssid = "current_ssid";
        let connected_to_bss_info = create_bssinfo_using_ssid(Some(current_ssid));

        let current_status = fidl_sme::ClientStatusResponse {
            connected_to: connected_to_bss_info,
            connecting_to_ssid: vec![],
        };

        assert!(is_connect_to_target_network_needed(
            stay_connected,
            target_ssid.as_bytes().to_vec(),
            &current_status
        ));
    }

    /// Test to verify a connection will be triggered for an SSID that is already connected.  This
    /// test is called with the target network already connected and stay_connected false.
    #[test]
    fn test_target_network_needs_connection_stay_connected_false() {
        let stay_connected = false;
        let target_ssid = "target_ssid";
        let connected_to_bss_info = create_bssinfo_using_ssid(Some(target_ssid));

        let current_status = fidl_sme::ClientStatusResponse {
            connected_to: connected_to_bss_info,
            connecting_to_ssid: vec![],
        };

        assert!(is_connect_to_target_network_needed(
            stay_connected,
            target_ssid.as_bytes().to_vec(),
            &current_status
        ));
    }

    /// Test to verify a connection will not be triggered for an SSID that is already connected.
    /// This is called with stay connected true with the target network already connected.
    #[test]
    fn test_target_network_does_not_need_connection() {
        let stay_connected = true;
        let target_ssid = "target_ssid";
        let connected_to_bss_info = create_bssinfo_using_ssid(Some(target_ssid));

        let current_status = fidl_sme::ClientStatusResponse {
            connected_to: connected_to_bss_info,
            connecting_to_ssid: vec![],
        };

        assert_eq!(
            is_connect_to_target_network_needed(
                stay_connected,
                target_ssid.as_bytes().to_vec(),
                &current_status
            ),
            false
        );
    }

    fn create_bssinfo_using_ssid<S: ToString>(ssid: Option<S>) -> Option<Box<fidl_sme::BssInfo>> {
        ssid.map(|s| {
            let bss_info: fidl_sme::BssInfo = fidl_sme::BssInfo {
                bssid: [0, 1, 2, 3, 4, 5],
                ssid: s.to_string().as_bytes().to_vec(),
                rx_dbm: -30,
                channel: 1,
                protected: true,
                compatible: true,
            };
            Box::new(bss_info)
        })
    }
}
