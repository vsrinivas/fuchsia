// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use device;
use fidl;
use futures::future;
use wlan_service::{self, DeviceService};

use std::cell::RefCell;
use std::rc::Rc;

pub struct DeviceServiceServer {
    devmgr: Rc<RefCell<device::DeviceManager>>,
}

impl DeviceServiceServer {
    pub fn new(devmgr: Rc<RefCell<device::DeviceManager>>) -> Self {
        DeviceServiceServer { devmgr: devmgr }
    }
}

impl DeviceService::Server for DeviceServiceServer {
    type ListPhys = future::FutureResult<wlan_service::ListPhysResponse, fidl::CloseChannel>;
    fn list_phys(&mut self) -> Self::ListPhys {
        info!("list_phys");
        let result = self.devmgr.borrow().list_phys();
        future::ok(wlan_service::ListPhysResponse { phys: result })
    }

    type ListIfaces = future::FutureResult<wlan_service::ListIfacesResponse, fidl::CloseChannel>;
    fn list_ifaces(&mut self) -> Self::ListIfaces {
        info!("list_ifaces (stub)");
        future::ok(wlan_service::ListIfacesResponse { ifaces: vec![] })
    }

    type CreateIface = future::FutureResult<wlan_service::CreateIfaceResponse, fidl::CloseChannel>;
    fn create_iface(&mut self, req: wlan_service::CreateIfaceRequest) -> Self::CreateIface {
        info!("create_iface req: {:?}", req);
        // TODO(tkilbourn): have DeviceManager return a future here
        let resp = self.devmgr.borrow_mut().create_iface(req.phy_id, req.role);
        match resp {
            Ok(resp) => future::ok(wlan_service::CreateIfaceResponse { iface_id: resp }),
            Err(_) => future::err(fidl::CloseChannel),
        }
    }

    type DestroyIface = future::FutureResult<(), fidl::CloseChannel>;
    fn destroy_iface(&mut self, req: wlan_service::DestroyIfaceRequest) -> Self::DestroyIface {
        info!("destroy_iface req: {:?}", req);
        // TODO(tkilbourn): have DeviceManager return a future here
        let resp = self.devmgr
            .borrow_mut()
            .destroy_iface(req.phy_id, req.iface_id);
        match resp {
            Ok(_) => future::ok(()),
            Err(_) => future::err(fidl::CloseChannel),
        }
    }
}
