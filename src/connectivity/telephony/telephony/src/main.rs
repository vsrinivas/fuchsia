// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! System service for managing cellular modems

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_telephony_manager::{ManagerRequest, ManagerRequestStream},
    fidl_fuchsia_telephony_ril::{
        RadioInterfaceLayerMarker, RadioInterfaceLayerProxy, SetupMarker,
    },
    fuchsia_async as fasync,
    fuchsia_component::{
        client::{launch, launcher, App},
        fuchsia_single_component_package_url,
        server::ServiceFs,
    },
    fuchsia_inspect as inspect,
    fuchsia_syslog::{self as syslog, macros::*},
    fuchsia_vfs_watcher::{WatchEvent, Watcher},
    fuchsia_zircon as zx,
    futures::{
        future::{self, join},
        Future, StreamExt, TryFutureExt, TryStreamExt,
    },
    io_util::{open_directory_in_namespace, OPEN_RIGHT_READABLE},
    parking_lot::RwLock,
    qmi::connect_transport_device,
    std::fs::File,
    std::path::{Path, PathBuf},
    std::sync::Arc,
    thiserror::Error,
};

const QMI_TRANSPORT: &str = "/dev/class/qmi-transport";
const RIL_URI: &str = fuchsia_single_component_package_url!("ril-qmi");

#[derive(Debug, PartialEq)]
pub enum ModemType {
    Qmi,
    //TODO(bwb): Other types of modem interfaces
    At, // Ex: Mbim, AT, etc...
}

#[derive(Error, Debug)]
pub enum TelError {
    #[error("telephony svc does not know how to work with {:?} yet", 0)]
    UnknownTransport(ModemType),
    #[error("Connection to a radio has failed")]
    FailedConnection(),
    #[error("The Radio Interface Layer has returned a error: {:?}", 0)]
    RilError(fidl_fuchsia_telephony_ril::RilError),
}

pub async fn connect_qmi_transport(path: PathBuf) -> Result<fasync::Channel, Error> {
    let file = File::open(&path)?;
    let chan = connect_transport_device(&file).await?;
    Ok(fasync::Channel::from_channel(chan)?)
}

pub async fn start_modem(
    ty: ModemType,
    chan: zx::Channel,
    node: inspect::Node,
    path: PathBuf,
) -> Result<Radio, Error> {
    let launcher = launcher().context("Failed to open launcher service")?;
    let app = launch(&launcher, RIL_URI.to_string(), None)
        .context("Failed to launch qmi-modem service")?;
    let setup_ril = app.connect_to_service::<SetupMarker>()?;
    let ril = app.connect_to_service::<RadioInterfaceLayerMarker>()?;
    match ty {
        ModemType::Qmi => match setup_ril.connect_transport(chan.into()).await? {
            Ok(_) => Ok(Radio::new(app, ril, node, path)),
            Err(e) => Err(TelError::RilError(e).into()),
        },
        t => return Err(TelError::UnknownTransport(t).into()),
    }
}

pub fn start_service(
    mgr: Arc<Manager>,
    stream: ManagerRequestStream,
) -> impl Future<Output = Result<(), Error>> {
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
                            let resp = radio.app.pass_to_service::<RadioInterfaceLayerMarker>(
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

    _node: inspect::Node,
    _uri_str_property: inspect::StringProperty,
}

impl Radio {
    pub fn new(
        app: App,
        ril: RadioInterfaceLayerProxy,
        node: inspect::Node,
        path: PathBuf,
    ) -> Self {
        let uri_str_property = node.create_string("URI", path.to_string_lossy().as_ref());
        Radio { app, ril, _node: node, _uri_str_property: uri_str_property }
    }
}

pub struct Manager {
    radios: RwLock<Vec<Radio>>,

    _node: inspect::Node,
    radios_node: inspect::Node,
}

impl Manager {
    pub fn new(node: inspect::Node) -> Self {
        let radios_node = node.create_child("radios");
        Manager { radios: RwLock::new(vec![]), _node: node, radios_node }
    }

    async fn watch_new_devices(&self) -> Result<(), Error> {
        // TODO(bwb): make more generic to support non-qmi devices
        let path: &Path = Path::new(QMI_TRANSPORT);
        let dir_proxy = open_directory_in_namespace(QMI_TRANSPORT, OPEN_RIGHT_READABLE)?;
        let mut watcher = Watcher::new(dir_proxy).await.unwrap();
        while let Some(msg) = watcher.try_next().await? {
            match msg.event {
                WatchEvent::EXISTING | WatchEvent::ADD_FILE => {
                    let qmi_path = path.join(msg.filename);
                    fx_log_info!("Connecting to {}", qmi_path.display());
                    let file = File::open(&qmi_path)?;
                    let channel = qmi::connect_transport_device(&file).await?;
                    let svc = start_modem(
                        ModemType::Qmi,
                        channel,
                        self.radios_node
                            .create_child(format!("radio-{}", self.radios.read().len())),
                        qmi_path,
                    )
                    .await?;
                    self.radios.write().push(svc);
                }
                WatchEvent::REMOVE_FILE => {
                    let qmi_path = path.join(msg.filename);
                    fx_log_info!("Disconnected from {}", qmi_path.display());
                    // TODO: (nj)remove the radios on disconnect will need a path to radio mapping.
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

    // Creates a new inspector object. This will create the "root" node in the
    // inspect tree to which further children objects can be added.
    let inspector = inspect::Inspector::new();

    let manager = Arc::new(Manager::new(inspector.root().create_child("manager")));
    let mgr = manager.clone();
    let device_watcher = manager
        .watch_new_devices()
        .unwrap_or_else(|e| fx_log_err!("Failed to watch new devices: {:?}", e));

    let mut fs = ServiceFs::new();

    fs.dir("svc").add_fidl_service(move |stream| {
        fx_log_info!("Spawning Management Interface");
        fasync::spawn(
            start_service(mgr.clone(), stream)
                .unwrap_or_else(|e| fx_log_err!("Failed to spawn {:?}", e)),
        )
    });

    // Serves the Inspect Tree at the standard location "/diagnostics/fuchsia.inspect.Tree"
    inspector.serve(&mut fs)?;

    fs.take_and_serve_directory_handle()?;

    let ((), ()) = executor.run_singlethreaded(join(device_watcher, fs.collect::<()>()));
    Ok(())
}

#[cfg(test)]
mod test {

    use {super::*, fuchsia_inspect::assert_inspect_tree};

    #[test]
    fn pass() -> () {
        ();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_manager_inspect_tree() -> Result<(), Error> {
        let inspector = inspect::Inspector::new();
        let manager = Arc::new(Manager::new(inspector.root().create_child("manager")));

        assert_inspect_tree!(inspector, root: {
            manager: {
                radios: {}
            }
        });

        {
            let launcher = launcher().context("Failed to open launcher service")?;
            let app = launch(&launcher, RIL_URI.to_string(), None)
                .context("Failed to launch ril-qmi service")?;
            let ril = app.connect_to_service::<RadioInterfaceLayerMarker>()?;
            let path = PathBuf::from(r"class/qmi-transport/000");
            let _radio1 = Radio::new(app, ril, manager.radios_node.create_child("radio-1"), path);
            assert_inspect_tree!(inspector, root: {
               manager: {
                   radios: {
                       "radio-1": {
                           URI: "class/qmi-transport/000"
                       }
                   }
               }
            });

            // Radio will be radio will be removed from inspect tree as it is out of scope/dropped
        }

        assert_inspect_tree!(inspector, root: {
            manager: {
                radios: {}
            }
        });

        let launcher = launcher().context("Failed to open launcher service")?;

        let app = launch(&launcher, RIL_URI.to_string(), None)
            .context("Failed to launch ril-qmi service")?;
        let ril = app.connect_to_service::<RadioInterfaceLayerMarker>()?;
        let path = PathBuf::from(r"class/qmi-transport/001");
        let node = manager.radios_node.create_child("radio-1");
        let _radio1 = Radio::new(app, ril, node, path);
        assert_inspect_tree!(inspector, root: {
            manager: {
                radios: {
                    "radio-1": {
                        URI: "class/qmi-transport/001"
                    }
                }
            }
        });

        let app2 = launch(&launcher, RIL_URI.to_string(), None)
            .context("Failed to launch ril-qmi service")?;
        let ril2 = app2.connect_to_service::<RadioInterfaceLayerMarker>()?;
        let path2 = PathBuf::from(r"class/qmi-transport/002");
        let node2 = manager.radios_node.create_child("radio-2");
        let _radio2 = Radio::new(app2, ril2, node2, path2);

        assert_inspect_tree!(inspector, root: {
            manager: {
                radios: {
                    "radio-1": {
                        URI: "class/qmi-transport/001"
                    },
                    "radio-2": {
                        URI: "class/qmi-transport/002"
                    }
                }
            }
        });

        Ok(())
    }
}
