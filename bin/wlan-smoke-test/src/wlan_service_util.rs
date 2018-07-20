// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{format_err, Error, ResultExt};
use fidl::endpoints2;
use fidl_fuchsia_wlan_device_service as wlan_service;
use fidl_fuchsia_wlan_sme as fidl_sme;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::future;
use futures::prelude::*;
use pin_utils::pin_mut;
use std::fmt;
use fidl_fuchsia_wlan_device_service::DeviceServiceProxy;

type WlanService = DeviceServiceProxy;

// Helper object to formate BSSIDs
pub struct Bssid(pub [u8; 6]);

impl fmt::Display for Bssid {
    fn fmt(&self, f: &mut fmt::Formatter) -> Result<(), fmt::Error> {
        write!(f, "{:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}",
               self.0[0], self.0[1], self.0[2], self.0[3], self.0[4], self.0[5])
    }
}

// Helper methods for calling wlan_service fidl methods

pub async fn get_iface_list(wlan_svc: &DeviceServiceProxy)
        -> Result<Vec<u16>, Error> {
    let response = await!(wlan_svc.list_ifaces()).context("Error getting iface list")?;
    let mut wlan_iface_ids = Vec::new();
    for iface in response.ifaces{
        wlan_iface_ids.push(iface.iface_id);
    }
    Ok(wlan_iface_ids)
}

pub async fn get_iface_sme_proxy(wlan_svc: &WlanService, iface_id: u16)
        -> Result<fidl_sme::ClientSmeProxy, Error> {
    let (sme_proxy, sme_remote) = endpoints2::create_endpoints()?;
    let status = await!(wlan_svc.get_client_sme(iface_id, sme_remote))
            .context("error sending GetClientSme request")?;
    if status == zx::sys::ZX_OK {
        Ok(sme_proxy)
    } else {
        Err(format_err!("Invalid interface id {}", iface_id))
    }
}

#[cfg(test)]
mod tests {

    use super::*;
    use fidl::endpoints2::RequestStream;
    use fidl_fuchsia_wlan_device_service as wlan_service;
    use fidl_fuchsia_wlan_device_service::{DeviceServiceMarker, DeviceServiceProxy};
    use fidl_fuchsia_wlan_device_service::{DeviceServiceRequest, DeviceServiceRequestStream};
    use fidl_fuchsia_wlan_device_service::{IfaceListItem, ListIfacesResponse};
    use fidl_fuchsia_wlan_sme::{ClientSmeRequest, ClientSmeRequestStream};
    use fuchsia_async as fasync;
    use futures::stream::StreamFuture;

    #[test]
    fn list_ifaces_returns_iface_id_vector() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let (wlan_service, mut server) = create_wlan_service_util();
        let mut next_device_service_req = server.into_future();

        // create the data to use in the response
        let iface_id_list: Vec<u16> = vec![0, 1, 35, 36];
        let mut iface_list_vec = vec![];
        for id in &iface_id_list {
            iface_list_vec.push(IfaceListItem{iface_id: *id, path:
                                              "/foo/bar/".to_string()});
        }

        let mut fut = get_iface_list(&wlan_service);
        pin_mut!(fut);
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        send_iface_list_response(&mut exec, &mut next_device_service_req, iface_list_vec);

        let complete = exec.run_until_stalled(&mut fut);

        let list_response = match complete {
            Poll::Ready(result) => result,
            _ => panic!("Expected an iface list response")
        };

        let response = match list_response {
            Ok(response) => response,
            Err(_) => panic!("Expected a valid list response")
        };

        // now verify the response
        assert_eq!(response, iface_id_list);
    }

    #[test]
    fn list_ifaces_properly_handles_zero_ifaces() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let (wlan_service, mut server) = create_wlan_service_util();
        let mut next_device_service_req = server.into_future();

        // create the data to use in the response
        let iface_id_list: Vec<u16> = vec![];
        let mut iface_list_vec = vec![];

        let mut fut = get_iface_list(&wlan_service);
        pin_mut!(fut);
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        send_iface_list_response(&mut exec, &mut next_device_service_req, iface_list_vec);

        let complete = exec.run_until_stalled(&mut fut);

        let list_response = match complete {
            Poll::Ready(result) => result,
            _ => panic!("Expected an iface list response")
        };

        let response = match list_response {
            Ok(response) => response,
            Err(_) => panic!("Expected a valid list response")
        };

        // now verify the response
        assert_eq!(response, iface_id_list);
    }

    fn poll_device_service_req(exec: &mut fasync::Executor,
            next_device_service_req: &mut StreamFuture<DeviceServiceRequestStream>)
        -> Poll<DeviceServiceRequest>
    {
        exec.run_until_stalled(next_device_service_req).map(|(req, stream)| {
            *next_device_service_req = stream.into_future();
            req.expect("did not expect the DeviceServiceRequestStream to end")
                .expect("error polling device service request stream")
        })
    }

    fn send_iface_list_response(exec: &mut fasync::Executor,
            server: &mut StreamFuture<wlan_service::DeviceServiceRequestStream>,
            iface_list_vec: Vec<IfaceListItem>)
    {
        let responder = match poll_device_service_req(exec, server) {
            Poll::Ready(DeviceServiceRequest::ListIfaces {responder}) => responder,
            Poll::Pending => panic!("expected a request to be available"),
            _ => panic!("expected a ListIfaces request"),
        };

        // now send the response back
        responder.send(&mut ListIfacesResponse{ifaces:iface_list_vec});
    }

    fn create_wlan_service_util()
            -> (DeviceServiceProxy, wlan_service::DeviceServiceRequestStream) {
        let (proxy, server) = endpoints2::create_endpoints::<DeviceServiceMarker>()
                .expect("failed to create a wlan_service channel for tests");
        let server = server.into_stream()
                .expect("failed to create a wlan_service response stream");
        (proxy, server)
    }
}

