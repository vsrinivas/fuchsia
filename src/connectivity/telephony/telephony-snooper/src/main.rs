// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! System service for snoop cellular modems
use {
    anyhow::{format_err, Context as _, Error},
    argh::FromArgs,
    fidl::endpoints::{Proxy, RequestStream, ServerEnd},
    fidl_fuchsia_telephony_snoop::{
        PublisherMarker as QmiSnoopMarker, PublisherRequest as QmiSnoopRequest,
        PublisherRequestStream as QmiSnoopRequestStream, SnooperControlHandle, SnooperRequest,
        SnooperRequestStream,
    },
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{self as syslog, macros::*},
    fuchsia_vfs_watcher::{WatchEvent, Watcher},
    futures::{future, StreamExt, TryFutureExt, TryStreamExt},
    parking_lot::Mutex,
    std::{
        fs::File,
        path::{Path, PathBuf},
        sync::Arc,
        vec::Vec,
    },
    tel_dev::isolated_devmgr,
};

const QMI_TRANSPORT: &str = "/dev/class/qmi-transport";

#[derive(Default)]
pub struct Snooper {
    control_handles: Vec<SnooperControlHandle>,
    device_num: u32,
}

#[derive(FromArgs, Debug)]
#[argh(description = "Snoop configs")]
pub struct Args {
    /// snoop driver loaded in Isolated Devmgr component
    #[argh(switch, short = 't')]
    pub use_isolated_devmgr: bool,
}

async fn watch_new_devices(
    snooper: Arc<Mutex<Snooper>>,
    path_in_dev: &Path,
    use_isolated_devmgr: bool,
) -> Result<(), Error> {
    // TODO(jiamingw): make more generic to support non-qmi devices
    let (protocol_path, dir) = if use_isolated_devmgr {
        (
            path_in_dev.strip_prefix("/dev")?,
            isolated_devmgr::open_dir_in_isolated_devmgr(path_in_dev.strip_prefix("/dev")?)
                .context("Opening dir in IsolatedDevmgr failed")?,
        )
    } else {
        (path_in_dev, File::open(path_in_dev).context("Opening dir in devmgr failed")?)
    };

    let channel = fdio::clone_channel(&dir).unwrap();
    let async_channel = fasync::Channel::from_channel(channel).unwrap();
    let directory = fidl_fuchsia_io::DirectoryProxy::from_channel(async_channel);
    let mut watcher =
        Watcher::new(directory).await.with_context(|| format!("could not watch {:?}", &dir))?;
    while let Some(msg) = watcher.try_next().await? {
        match msg.event {
            WatchEvent::IDLE => {
                fx_log_info!("watch_new_devices: all devices enumerated");
            }
            WatchEvent::REMOVE_FILE => {
                snooper.lock().device_num -= 1;
                fx_log_info!("watch_new_devices: device removed");
            }
            WatchEvent::EXISTING | WatchEvent::ADD_FILE => {
                let device_path: PathBuf = protocol_path.join(msg.filename);
                fx_log_info!("watch_new_devices: connecting to {}", device_path.display());
                let file: File = if use_isolated_devmgr {
                    isolated_devmgr::open_file_in_isolated_devmgr(device_path)?
                } else {
                    File::open(device_path)?
                };
                let snoop_endpoint_server_side: ServerEnd<QmiSnoopMarker> =
                    qmi::connect_snoop_channel(&file).await?;
                let snooper_cloned = snooper.clone();
                let mut request_stream: QmiSnoopRequestStream =
                    snoop_endpoint_server_side.into_stream()?;
                snooper.lock().device_num += 1;
                fasync::spawn(async move {
                    fx_log_info!("watch_new_devices: spawn async block for forwarding msg");
                    while let Ok(Some(QmiSnoopRequest::SendMessage {
                        mut msg,
                        control_handle: _,
                    })) = request_stream.try_next().await
                    {
                        let mut snooper_locked = snooper_cloned.lock();
                        fx_log_info!(
                            "watch_new_devices: qmi msg rcvd, forwarding to {} client...",
                            snooper_locked.control_handles.len()
                        );
                        // try to send message to all clients connected to snooper
                        // remove client's control handle if there is any error
                        let mut removed_reason = Vec::<fidl::Error>::new();
                        snooper_locked.control_handles.retain(|ctl_hdl| {
                            if let Err(e) = ctl_hdl.send_on_message(&mut msg) {
                                removed_reason.push(e);
                                false
                            } else {
                                true
                            }
                        });
                        if removed_reason.len() > 0 {
                            fx_log_info!(
                                "watch_new_devices: removed {} hdl with reason {:?}",
                                removed_reason.len(),
                                removed_reason
                            );
                        }
                    }
                    fx_log_info!("watch_new_devices: stop forwarding msg");
                });
            }
            _ => {
                return Err(format_err!("watch_new_devices: unknown watcher event"));
            }
        }
    }
    fx_log_err!("watch new devices terminated");
    Ok(())
}

// forwarding QMI messages from driver to snooper client
#[fasync::run_singlethreaded]
async fn main() {
    syslog::init_with_tags(&["tel-snooper"]).expect("Can't init logger");
    fx_log_info!("Starting telephony snoop service...");
    let args: Args = argh::from_env();
    let snooper = Arc::new(Mutex::new(Snooper { control_handles: vec![], device_num: 0 }));
    let qmi_device_path: &Path = Path::new(QMI_TRANSPORT);
    let qmi_device_watcher =
        watch_new_devices(snooper.clone(), qmi_device_path, args.use_isolated_devmgr)
            .unwrap_or_else(|e| fx_log_err!("Failed to watch new devices: {:?}", e));
    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(move |mut stream: SnooperRequestStream| {
        fx_log_info!("new client connect to Snooper");
        snooper.lock().control_handles.push(stream.control_handle());
        let snooper_clone = snooper.clone();
        fasync::spawn(
            async move {
                while let Some(req) = (stream.try_next()).await? {
                    match req {
                        SnooperRequest::GetDeviceNum { responder } => {
                            if let Err(e) = responder.send(snooper_clone.lock().device_num) {
                                fx_log_err!("failed to respond with device number {:?}", e);
                            }
                        }
                    }
                }
                Ok(())
            }
            .unwrap_or_else(|e: anyhow::Error| fx_log_err!("{:?}", e)),
        );
    });
    fs.take_and_serve_directory_handle().expect("ServiceFs failed to serve directory");
    future::join(fs.collect::<()>(), qmi_device_watcher).await;
}
