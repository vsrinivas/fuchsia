// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::driver::srp_proxy::LOCAL_DOMAIN;
use crate::prelude::*;
use parking_lot::Mutex;
use std::collections::HashMap;
use std::ffi::{CStr, CString};
use std::sync::Arc;

/// The advertising proxy handles taking hosts and services registered with the SRP server
/// and republishing them via local mDNS.
#[derive(Debug)]
pub struct AdvertisingProxy {
    inner: Arc<Mutex<AdvertisingProxyInner>>,
}

impl Drop for AdvertisingProxy {
    fn drop(&mut self) {
        // Make sure all advertised hosts get cleaned up.
        self.inner.lock().hosts.clear();
    }
}

#[derive(Debug)]
struct AdvertisingProxyInner {
    hosts: HashMap<CString, AdvertisingProxyHost>,
}

#[derive(Debug)]
pub struct AdvertisingProxyHost {
    services: HashMap<CString, AdvertisingProxyService>,
}

#[derive(Debug)]
pub struct AdvertisingProxyService {
    txt_data: Vec<u8>,
    port: u16,
    priority: u16,
    weight: u16,
}

impl AdvertisingProxyService {
    fn is_up_to_date(&self, srp_service: &ot::SrpServerService) -> bool {
        !srp_service.is_deleted()
            && self.txt_data == srp_service.txt_data()
            && self.weight == srp_service.weight()
            && self.priority == srp_service.priority()
            && self.port == srp_service.port()
    }
}

impl AdvertisingProxy {
    pub fn new(instance: &ot::Instance) -> Result<AdvertisingProxy, anyhow::Error> {
        let inner = Arc::new(Mutex::new(AdvertisingProxyInner { hosts: Default::default() }));
        let ret = AdvertisingProxy { inner: inner.clone() };

        ret.inner.lock().publish_srp_all(instance)?;

        instance.srp_server_set_service_update_fn(Some(
            move |ot_instance: &ot::Instance,
                  update_id: ot::SrpServerServiceUpdateId,
                  host: &ot::SrpServerHost,
                  _timeout: u32| {
                info!("srp_server_set_service_update: Update for {:?}", host);
                ot_instance.srp_server_handle_service_update_result(
                    update_id,
                    inner.lock().publish_srp_host(instance, host).map_err(|e| {
                        warn!(
                            "srp_server_set_service_update: Error publishing {:?}: {:?}",
                            host, e
                        );
                        ot::Error::Failed
                    }),
                );
            },
        ));

        Ok(ret)
    }
}

impl AdvertisingProxyInner {
    pub fn publish_srp_all(&mut self, instance: &ot::Instance) -> Result<(), anyhow::Error> {
        for host in instance.srp_server_hosts() {
            if let Err(err) = self.publish_srp_host(instance, host) {
                warn!(
                    "Unable to fully publish SRP host {:?} to mDNS: {:?}",
                    host.full_name_cstr(),
                    err
                );
            }
        }

        Ok(())
    }

    /// Updates the mDNS service with the host and services from the SrpServerHost.
    pub fn publish_srp_host(
        &mut self,
        _instance: &ot::Instance,
        srp_host: &ot::SrpServerHost,
    ) -> Result<(), anyhow::Error> {
        if srp_host.is_deleted() {
            // Delete the host.
            info!("No longer advertising {:?} on {:?}", srp_host.full_name_cstr(), LOCAL_DOMAIN);

            self.hosts.remove(srp_host.full_name_cstr());
            return Ok(());
        }

        let host: &mut AdvertisingProxyHost =
            if let Some(host) = self.hosts.get_mut(srp_host.full_name_cstr()) {
                // Use the existing host.
                info!(
                    "Updating advertisement of {:?} on {:?}",
                    srp_host.full_name_cstr(),
                    LOCAL_DOMAIN
                );

                host
            } else {
                // Add the host.
                info!("Advertising {:?} on {:?}", srp_host.full_name_cstr(), LOCAL_DOMAIN);

                self.hosts.insert(
                    srp_host.full_name_cstr().to_owned(),
                    AdvertisingProxyHost { services: Default::default() },
                );
                self.hosts.get_mut(srp_host.full_name_cstr()).unwrap()
            };

        let services = &mut host.services;

        for srp_service in srp_host.find_services::<&CStr, &CStr>(
            ot::SrpServerServiceFlags::BASE_TYPE_SERVICE_ONLY,
            None,
            None,
        ) {
            if srp_service.is_deleted() {
                // Delete the service.
                info!(
                    "No longer advertising {:?} on {:?}",
                    srp_service.full_name_cstr(),
                    LOCAL_DOMAIN
                );
                services.remove(srp_service.full_name_cstr());
                continue;
            }

            let is_up_to_date = services
                .get(srp_service.full_name_cstr())
                .map(|s| s.is_up_to_date(srp_service))
                .unwrap_or(false);

            if is_up_to_date {
                // Service is already up to date.
                continue;
            }

            info!("Advertising {:?} on {:?}", srp_service.full_name_cstr(), LOCAL_DOMAIN);

            // (Re-)Add the service.

            services.insert(
                srp_service.full_name_cstr().to_owned(),
                AdvertisingProxyService {
                    txt_data: srp_service.txt_data().to_vec(),
                    port: srp_service.port(),
                    priority: srp_service.priority(),
                    weight: srp_service.weight(),
                },
            );
        }

        Ok(())
    }
}
