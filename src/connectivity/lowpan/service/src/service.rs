// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

use anyhow::Context as _;
use fidl_fuchsia_lowpan_device::{
    DeviceChanges, DriverMarker, DriverProxy, LookupRequest, LookupRequestStream, RegisterRequest,
    RegisterRequestStream, ServiceError, MAX_LOWPAN_DEVICES,
};
use fuchsia_syslog::macros::*;
use futures::prelude::*;
use futures::task::{Spawn, SpawnExt};
use lowpan_driver_common::AsyncCondition;
use parking_lot::Mutex;
use regex::Regex;
use std::collections::HashMap;
use std::sync::Arc;

lazy_static::lazy_static! {
    static ref DEVICE_NAME_REGEX: Regex = Regex::new("^[a-z_][-_.+0-9a-z]{1,31}$")
        .expect("Device name regex failed to compile");
}

pub struct LowpanService<S> {
    pub devices: Arc<Mutex<HashMap<String, DriverProxy>>>,
    pub added_removed_cond: Arc<AsyncCondition>,
    pub spawner: S,
}

impl<S: Spawn> LowpanService<S> {
    pub fn with_spawner(spawner: S) -> LowpanService<S> {
        LowpanService {
            devices: Default::default(),
            added_removed_cond: Default::default(),
            spawner,
        }
    }
}

impl<S> LowpanService<S> {
    pub fn lookup(&self, name: &str) -> Result<DriverProxy, ServiceError> {
        let devices = self.devices.lock();
        if let Some(device) = devices.get(name) {
            Ok(device.clone())
        } else {
            Err(ServiceError::DeviceNotFound)
        }
    }

    pub fn get_devices(&self) -> Vec<String> {
        let devices = self.devices.lock();
        devices.keys().into_iter().map(|x| x.to_string()).collect()
    }
}

impl<S: Spawn> LowpanService<S> {
    pub fn register(
        &self,
        name: &str,
        driver: fidl::endpoints::ClientEnd<DriverMarker>,
    ) -> Result<(), ServiceError> {
        let driver = driver.into_proxy().map_err(|_| ServiceError::InvalidArgument)?;

        if !DEVICE_NAME_REGEX.is_match(name) {
            fx_log_err!("Attempted to register LoWPAN device with invalid name {:?}", name);
            return Err(ServiceError::InvalidInterfaceName);
        }

        let name = name.to_string();

        {
            // Lock the device list.
            let mut devices = self.devices.lock();

            // Check to make sure there already aren't too many devices.
            if devices.len() >= MAX_LOWPAN_DEVICES as usize {
                return Err(ServiceError::TooManyDevices);
            }

            // Check for existing devices with the same name.
            if devices.contains_key(&name) {
                return Err(ServiceError::DeviceAlreadyExists);
            }

            // Insert the new device into the list.
            devices.insert(name.clone(), driver.clone());
        }

        // Indicate that a new device was added.
        self.added_removed_cond.trigger();

        let devices = self.devices.clone();
        let added_removed_cond = self.added_removed_cond.clone();

        // The following code provides a way to automatically
        // remove a device when the connection to the LoWPAN Driver
        // is lost.
        let cleanup_task = driver
            .take_event_stream()
            .for_each(|_| futures::future::ready(()))
            .inspect(move |_: &()| {
                fx_log_info!("Removing device {:?}", &name);

                devices.lock().remove(&name);

                // Indicate that the device was removed.
                added_removed_cond.trigger();
            });

        self.spawner.spawn(cleanup_task).expect("Unable to spawn cleanup task");

        Ok(())
    }
}

#[async_trait::async_trait(?Send)]
impl<S> ServeTo<LookupRequestStream> for LowpanService<S> {
    async fn serve_to(&self, request_stream: LookupRequestStream) -> anyhow::Result<()> {
        use futures::lock::Mutex;
        let last_device_list: Mutex<Option<Vec<String>>> = Mutex::new(None);

        request_stream
            .err_into::<Error>()
            .try_for_each_concurrent(MAX_CONCURRENT, |command| async {
                match command {
                    LookupRequest::LookupDevice { name, protocols, responder } => {
                        fx_log_info!("Received lookup request for {:?}", name);

                        let mut ret = self.lookup(&name).and_then(|dev| {
                            dev.get_protocols(protocols).map_err(|_| ServiceError::DeviceNotFound)
                        });

                        responder.send(&mut ret)?;

                        fx_log_info!("Responded to lookup request {:?}", name);
                    }

                    LookupRequest::GetDevices { responder } => {
                        fx_log_info!("Received get devices request");
                        responder
                            .send(&mut self.get_devices().iter().map(|s| &**s))
                            .context("error sending response")?;
                        fx_log_info!("Responded to get devices request");
                    }

                    LookupRequest::WatchDevices { responder } => {
                        let mut locked_device_list =
                            last_device_list.try_lock().ok_or(format_err!(
                                "No more than 1 outstanding call to watch_devices is allowed"
                            ))?;

                        if locked_device_list.is_none() {
                            // This is the first call to WatchDevices,
                            // so we return the whole list.

                            *locked_device_list = Some(self.get_devices());

                            let mut device_changes = DeviceChanges {
                                added: locked_device_list.clone().unwrap(),
                                removed: vec![],
                            };

                            responder
                                .send(&mut device_changes)
                                .context("error sending response")?;
                        } else {
                            // This is a follow-up call.
                            let current_devices = loop {
                                let wait = self.added_removed_cond.wait();

                                let current_devices = self.get_devices();

                                // Note that this should work even though the returned
                                // list of interfaces isn't sorted. As long as the
                                // keys aren't intentionally shuffled when nothing
                                // has changed then this check should work just fine.
                                if current_devices != *locked_device_list.as_ref().unwrap() {
                                    break current_devices;
                                }

                                // We wait here for something to change.
                                wait.await;
                            };

                            // Devices have been added or removed, let's sort them out.
                            let mut device_changes = DeviceChanges {
                                // Calculate devices added.
                                // This mechanism is O(n^2), but in reality n is going to
                                // almost always be 1---so it makes sense to prioritize
                                // convenience. It may even be slower to try to optimize
                                // this.
                                added: current_devices
                                    .iter()
                                    .filter_map(|name| {
                                        if !locked_device_list.as_ref().unwrap().contains(name) {
                                            Some(name.clone())
                                        } else {
                                            None
                                        }
                                    })
                                    .collect(),

                                // Calculate devices removed.
                                // This mechanism is O(n^2), but in reality n is going to
                                // almost always be 1---so it makes sense to prioritize
                                // convenience. It may even be slower to try to optimize
                                // this.
                                removed: locked_device_list
                                    .as_ref()
                                    .unwrap()
                                    .iter()
                                    .filter_map(|name| {
                                        if !current_devices.contains(name) {
                                            Some(name.clone())
                                        } else {
                                            None
                                        }
                                    })
                                    .collect(),
                            };

                            // Save our current list of devices so that we
                            // can use it the next time this method is called.
                            *locked_device_list = Some(current_devices);

                            responder
                                .send(&mut device_changes)
                                .context("error sending response")?;
                        }
                    }
                }
                Result::<(), anyhow::Error>::Ok(())
            })
            .inspect_err(|e| fx_log_err!("{:?}", e))
            .await
    }
}

#[async_trait::async_trait(?Send)]
impl<S: Spawn> ServeTo<RegisterRequestStream> for LowpanService<S> {
    async fn serve_to(&self, request_stream: RegisterRequestStream) -> anyhow::Result<()> {
        request_stream
            .err_into::<Error>()
            .try_for_each_concurrent(MAX_CONCURRENT, |command| async {
                match command {
                    RegisterRequest::RegisterDevice { name, driver, responder } => {
                        fx_log_info!("Received register request for {:?}", name);

                        let mut response = self.register(&name, driver);

                        responder
                            .send(&mut response)
                            .context("error sending response to register request")?;

                        fx_log_info!("Responded to lookup request {:?}", name);
                    }
                }
                Result::<(), anyhow::Error>::Ok(())
            })
            .inspect_err(|e| fx_log_err!("{:?}", e))
            .await
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::create_endpoints;
    use fuchsia_async as fasync;

    #[fasync::run_until_stalled(test)]
    async fn test_interface_name_check() {
        let service = LowpanService::with_spawner(FuchsiaGlobalExecutor);

        let (client_ep, _) = create_endpoints::<DriverMarker>().unwrap();
        assert_eq!(service.register("lowpan0", client_ep), Ok(()));

        let (client_ep, _) = create_endpoints::<DriverMarker>().unwrap();
        assert_eq!(
            service.register("low pan 0", client_ep),
            Err(ServiceError::InvalidInterfaceName)
        );

        let (client_ep, _) = create_endpoints::<DriverMarker>().unwrap();
        assert_eq!(service.register("0lowpan", client_ep), Err(ServiceError::InvalidInterfaceName));

        let (client_ep, _) = create_endpoints::<DriverMarker>().unwrap();
        assert_eq!(service.register("l", client_ep), Err(ServiceError::InvalidInterfaceName));
    }

    #[fasync::run_until_stalled(test)]
    async fn test_interface_added_notifications() {
        let service = LowpanService::with_spawner(FuchsiaGlobalExecutor);

        let waiter = service.added_removed_cond.wait();

        let (driver_client_ep, _) = create_endpoints::<DriverMarker>().unwrap();
        assert_eq!(service.register("lowpan0", driver_client_ep), Ok(()));

        waiter.await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_interface_removed_notifications() {
        let service = LowpanService::with_spawner(FuchsiaGlobalExecutor);

        let mut waiter = service.added_removed_cond.wait();
        {
            let (driver_client_ep, _) = create_endpoints::<DriverMarker>().unwrap();
            assert_eq!(service.register("lowpan0", driver_client_ep), Ok(()));

            waiter.await;

            waiter = service.added_removed_cond.wait();
        }
        waiter.await;
    }
}
