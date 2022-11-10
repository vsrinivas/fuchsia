// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::{Arc, Weak};

use fidl_fuchsia_ui_app as fuiapp;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_component::server::ServiceObjLocal;
use fuchsia_zircon as zx;
use fuchsia_zircon::AsHandleRef;
use futures::channel::mpsc::{unbounded, UnboundedReceiver, UnboundedSender};
use futures::StreamExt;
use futures::TryStreamExt;
use wayland_bridge::dispatcher::WaylandDispatcher;

use super::bridge_client::*;
use super::file_creation::*;
use crate::auth::FsCred;
use crate::device::wayland::image_file::ImageFile;
use crate::device::wayland::BufferCollectionFile;
use crate::fs::buffers::*;
use crate::fs::socket::*;
use crate::fs::*;
use crate::logging::{log_error, log_warn};
use crate::task::{CurrentTask, Kernel};
use crate::types::*;

/// The services that are exposed in the component's outgoing directory by the wayland server.
enum ExposedServices {
    ViewProvider(fuiapp::ViewProviderRequestStream),
}

/// Initializes the wayland server.
///
/// This does a few different things:
///   - Serves a `ViewProvider` protocol on behalf of the component that is running. This view
///     provider connection is proxied to a view provider that is served by the wayland bridge
///     library.
///   - Creates a socket representing the wayland display. Data written to this socket is proxied
///     to the wayland bridge, which then executes the appropriate scenic commands.
///   - Creates a `DMABuf` file that the wayland client can use to allocate memory and share it with
///     the wayland bridge (and scenic).
///
/// # Parameters
/// - `task`: The task that is being run, which is used to create the wayland files.
/// - `display_path`: The path at which the wayland display socket is created.
/// - `device_path`: The path at which the `DMABuf` file is created.
pub fn serve_wayland(
    current_task: &CurrentTask,
    display_path: FsString,
    device_path: FsString,
    outgoing_dir: &mut Option<fidl::endpoints::ServerEnd<fidl_fuchsia_io::DirectoryMarker>>,
) -> Result<(), Errno> {
    let display_socket = create_display_socket(current_task, display_path)?;
    create_device_file(current_task, device_path)?;

    let kernel = current_task.thread_group.kernel.clone();
    let outgoing_dir_channel = outgoing_dir.take().ok_or_else(|| errno!(EINVAL))?;

    // Add `ViewProvider` to the exposed services of the component, and then serve the
    // outgoing directory.
    let mut outgoing_dir = ServiceFs::new_local();
    outgoing_dir.dir("svc").add_fidl_service(ExposedServices::ViewProvider);
    outgoing_dir.serve_connection(outgoing_dir_channel).map_err(|_| errno!(EINVAL))?;

    let (wayland_sender, wayland_server_receiver) = unbounded();
    let (wayland_server_sender, wayland_receiver) = unbounded();

    let kernel = Arc::downgrade(&kernel);

    // Spawn a thread to accept a connection to the wayland display socket.
    let _accept_socket_thread = std::thread::spawn(move || -> Result<(), Errno> {
        let socket = loop {
            if let Ok(socket) = display_socket.accept() {
                break socket;
            }
        };

        // Spawn a thread that reads data from the socket and sends it to the wayland bridge
        // via wayland_sender.
        let client_socket = socket.clone();
        let _read_socket_thread = std::thread::spawn(move || loop {
            handle_client_data(&client_socket, &wayland_sender);
        });

        // Reuse this thread to read data from the wayland_receiver (i.e., wayland protocol messages
        // from the wayland bridge library, meant for the wayland client).
        let mut executor = fasync::LocalExecutor::new().map_err(|_| errno!(EINVAL))?;
        executor.run_singlethreaded(handle_server_data(kernel, socket, wayland_receiver))?;

        Ok(())
    });

    let (view_provider_sender, view_provider_receiver) = unbounded();
    let wayland_server = Arc::new(parking_lot::Mutex::new(
        WaylandClient::new_view_producer_client(view_provider_sender),
    ));
    let dispatcher = WaylandDispatcher::new_local(wayland_server).map_err(|_| errno!(EINVAL))?;
    let display = &dispatcher.display;

    display.clone().spawn_new_local_client(
        wayland_server_sender,
        wayland_server_receiver,
        cfg!(feature = "wayland_protocol_logging"),
    );

    fasync::Task::local(serve_view_provider(outgoing_dir, view_provider_receiver, dispatcher))
        .detach();

    Ok(())
}

/// Serves a `ViewProvider` protocol from `outgoing_dir`. The outgoing directory belongs to the
/// component this starnix instance is running.
/// # Parameters
/// - `outgoing_dir`: The outgoing directory belonging to the component this starnix instance is
///                   running.
/// - `view_provider_receiver`: The channel endpoint that is used to wait for the wayland bridge
///                             library to create a `ViewProviderProxy`.
/// - `dispatcher`: The wayland bridge dispatcher that is handling the wayland protocol messages.
///                 The dispatcher is kept alive until the `outgoing_dir` is closed.
async fn serve_view_provider(
    mut outgoing_dir: ServiceFs<ServiceObjLocal<'static, ExposedServices>>,
    mut view_provider_receiver: UnboundedReceiver<fuiapp::ViewProviderProxy>,
    _dispatcher: WaylandDispatcher,
) {
    // The view provider that is received from the wayland bridge. Declared in the outer scope to
    // keep the proxy alive even after handling a create view request.
    let mut view_provider: Option<fuiapp::ViewProviderProxy>;

    while let Some(ExposedServices::ViewProvider(mut request_stream)) = outgoing_dir.next().await {
        // Wait for the wayland bridge to create a view provider on behalf of the component before
        // serving the view provider request stream.
        view_provider = view_provider_receiver.next().await;

        while let Ok(Some(event)) = request_stream.try_next().await {
            match event {
                fuiapp::ViewProviderRequest::CreateViewWithViewRef {
                    token,
                    mut view_ref_control,
                    mut view_ref,
                    ..
                } => {
                    if let Some(view_provider) = view_provider.as_ref() {
                        match view_provider.create_view_with_view_ref(
                            token,
                            &mut view_ref_control,
                            &mut view_ref,
                        ) {
                            Ok(_) => {}
                            Err(e) => {
                                log_error!("Got an error when creating view: {:?}", e);
                            }
                        }
                    };
                }
                fuiapp::ViewProviderRequest::CreateView2 { args, control_handle: _ } => {
                    if let Some(view_provider) = view_provider.as_ref() {
                        match view_provider.create_view2(args) {
                            Ok(_) => {}
                            Err(e) => {
                                log_error!("Got an error when creating view: {:?}", e);
                            }
                        }
                    };
                }
                r => {
                    log_warn!("Got unexpected view provider request: {:?}", r);
                }
            }
        }
    }
}

/// Reads wayland protocol data from `display_socket` and proxies it to the wayland bridge library.
///
/// # Parameters
/// - `display_socket`: The socket that the wayland client is connected to.
/// - `wayland_sender`: The sender used to send data to the wayland bridge.
fn handle_client_data(
    display_socket: &SocketHandle,
    wayland_sender: &UnboundedSender<zx::MessageBuf>,
) {
    let messages =
        display_socket.blocking_read_kernel().expect("Failed to wait for display socket data.");

    for message in messages {
        let mut handles = vec![];
        for ancillary_data in message.ancillary_data {
            match ancillary_data {
                AncillaryData::Unix(UnixControlData::Rights(files)) => {
                    for file in files {
                        if let Some(buffer_collection_file) =
                            file.downcast_file::<BufferCollectionFile>()
                        {
                            let import_token = buffer_collection_file
                                .token
                                .value
                                .as_handle_ref()
                                .duplicate(zx::Rights::SAME_RIGHTS)
                                .expect("Failed to duplicate buffer collection import token.");
                            handles.push(import_token);
                        } else if let Some(image_file) = file.downcast_file::<ImageFile>() {
                            if let Some(token) = &image_file.info.token {
                                let import_token = token
                                    .value
                                    .as_handle_ref()
                                    .duplicate(zx::Rights::SAME_RIGHTS)
                                    .expect("Failed to duplicate buffer collection import token.");
                                handles.push(import_token);
                            } else {
                                log_error!("No image file token.");
                            }
                        } else {
                            log_error!(
                                "Trying to parse buffre collection token from invalid file type."
                            );
                        }
                    }
                }
                _ => {
                    log_error!("Got unexpected ancillary data.");
                    return;
                }
            }
        }

        let message_buf = zx::MessageBuf::new_with(message.data.bytes().to_vec(), handles);
        match wayland_sender.unbounded_send(message_buf) {
            Ok(_) => {}
            Err(_) => {
                log_error!("Failed to send data to wayland bridge.");
                return;
            }
        }
    }
}

/// Reads data from the wayland bridge and sends it to the wayland client via the display socket.
///
/// # Parameters
/// - `kernel`: The kernel used to allocate files.
/// - `display_socket`: The display socket that the wayland client is connected to.
/// - `wayland_receiver`: The receiver to which wayland bridge sends data.
async fn handle_server_data(
    kernel: Weak<Kernel>,
    display_socket: SocketHandle,
    mut wayland_receiver: UnboundedReceiver<zx::MessageBuf>,
) -> Result<(), Errno> {
    while let Some(mut buffer) = wayland_receiver.next().await {
        if let Some(kernel) = kernel.upgrade() {
            let mut files: Vec<FileHandle> = vec![];

            // Create vmo files for each of the provided handles.
            while let Some(handle) = buffer.take_handle(0) {
                let vmo = zx::Vmo::from(handle);
                let file = FileObject::new_anonymous(
                    Box::new(VmoFileObject::new(Arc::new(vmo))),
                    anon_fs(&kernel).create_node(
                        Box::new(Anon),
                        FileMode::from_bits(0o600),
                        FsCred::root(),
                    ),
                    OpenFlags::RDWR,
                );
                files.push(file);
            }

            let ancillary_data = if !files.is_empty() {
                vec![AncillaryData::Unix(UnixControlData::Rights(files))]
            } else {
                vec![]
            };

            let message = Message::new(buffer.bytes().to_vec().into(), None, ancillary_data);
            display_socket.write_kernel(message)?;
        } else {
            return Ok(());
        }
    }

    Ok(())
}
