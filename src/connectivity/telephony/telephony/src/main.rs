// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! System service for managing cellular modems

#![feature(async_await, await_macro, futures_api)]
#![deny(warnings)]

use {
    failure::{Error, ResultExt},
    fidl::endpoints::{RequestStream, ServiceMarker},
    fidl_fuchsia_telephony_manager::{ManagerMarker, ManagerRequest, ManagerRequestStream},
    fidl_fuchsia_telephony_ril::{RadioInterfaceLayerMarker, RadioInterfaceLayerProxy},
    fuchsia_app::{
        client::{App, Launcher},
        fuchsia_single_component_package_url,
        server::ServicesServer,
    },
    fuchsia_async as fasync,
    fuchsia_syslog::{self as syslog, macros::*},
    fuchsia_vfs_watcher::{WatchEvent, Watcher},
    fuchsia_zircon as zx,
    futures::{future, Future, TryFutureExt, TryStreamExt},
    parking_lot::RwLock,
    qmi::connect_transport_device,
    std::fs::File,
    std::path::{Path, PathBuf},
    std::sync::Arc,
};

const QMI_TRANSPORT: &str = "/dev/class/qmi-transport";
const RIL_URI: &str = fuchsia_single_component_package_url!("ril-qmi");

pub fn connect_qmi_transport(path: PathBuf) -> Result<fasync::Channel, zx::Status> {
    let file = File::open(&path)?;
    let chan = connect_transport_device(&file)?;
    Ok(fasync::Channel::from_channel(chan)?)
}

pub async fn start_qmi_modem(chan: zx::Channel) -> Result<Radio, Error> {
    let launcher = Launcher::new().context("Failed to open launcher service")?;
    let app =
        launcher.launch(RIL_URI.to_string(), None).context("Failed to launch qmi-modem service")?;
    let ril = app.connect_to_service(RadioInterfaceLayerMarker)?;
    let _success = await!(ril.connect_transport(chan.into()))?;
    Ok(Radio::new(app, ril))
}

pub fn start_service(
    mgr: Arc<Manager>,
    channel: fasync::Channel,
) -> impl Future<Output = Result<(), Error>> {
    let stream = ManagerRequestStream::from_channel(channel);
    stream
        .try_for_each(move |evt| {
            let _ = match evt {
                ManagerRequest::IsAvailable { responder } => {
                    responder.send(!mgr.radios.read().is_empty())
                }
                // TODO(bwb): Get based on iface id, not just first one
                ManagerRequest::GetRilHandle { ril_iface, responder } => {
                    fx_log_info!("Vending a RIL handle to another process");
                    let radios = mgr.radios.read();
                    match radios.first() {
                        Some(radio) => {
                            let resp = radio.app.pass_to_service(
                                RadioInterfaceLayerMarker,
                                ril_iface.into_channel(),
                            );
                            responder.send(resp.is_ok())
                        }
                        None => responder.send(false),
                    }
                }
            };
            future::ready(Ok(()))
        })
        .map_err(|e| e.into())
}

pub struct Radio {
    pub app: App,
    // TODO(bwb) Deref into Ril proxy?
    #[allow(dead_code)]
    // TODO(bwb) remove dead_code, needed to retain ownership for now.
    ril: RadioInterfaceLayerProxy,
}

impl Radio {
    pub fn new(app: App, ril: RadioInterfaceLayerProxy) -> Self {
        Radio { app, ril }
    }
}

pub struct Manager {
    radios: RwLock<Vec<Radio>>,
}

impl Manager {
    pub fn new() -> Self {
        Manager { radios: RwLock::new(vec![]) }
    }

    async fn watch_new_devices(&self) -> Result<(), Error> {
        // TODO(bwb): make more generic to support non-qmi devices
        let path: &Path = Path::new(QMI_TRANSPORT);
        let dir = File::open(QMI_TRANSPORT).unwrap();
        let mut watcher = Watcher::new(&dir).unwrap();
        while let Some(msg) = await!(watcher.try_next())? {
            match msg.event {
                WatchEvent::EXISTING | WatchEvent::ADD_FILE => {
                    let qmi_path = path.join(msg.filename);
                    fx_log_info!("Connecting to {}", qmi_path.display());
                    let file = File::open(&qmi_path)?;
                    let channel = qmi::connect_transport_device(&file)?;
                    let svc = await!(start_qmi_modem(channel))?;
                    self.radios.write().push(svc);
                }
                _ => (),
            }
        }
        Ok(())
    }
}

fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["telephony"]).expect("Can't init logger");
    fx_log_info!("Starting telephony management service...");
    let mut executor = fasync::Executor::new().context("Error creating executor")?;

    let manager = Arc::new(Manager::new());
    let mgr = manager.clone();
    let device_watcher = manager.watch_new_devices();

    let server = ServicesServer::new()
        .add_service((ManagerMarker::NAME, move |chan: fasync::Channel| {
            fx_log_info!("Spawning Management Interface");
            fasync::spawn(
                start_service(mgr.clone(), chan)
                    .unwrap_or_else(|e| fx_log_err!("Failed to spawn {:?}", e)),
            )
        }))
        .start()?;

    executor.run_singlethreaded(device_watcher.try_join(server)).map(|_| ())
}
