// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::{Arc, Weak};

use anyhow::{anyhow, Error};
use fidl_fuchsia_ui_app as fuiapp;
use fidl_fuchsia_virtualization as fvirt;
use fidl_fuchsia_wayland as fwayland;
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_protocol;
use fuchsia_component::server::ServiceFs;
use fuchsia_component::server::ServiceObjLocal;
use fuchsia_zircon as zx;
use fuchsia_zircon::AsHandleRef;
use futures::StreamExt;
use futures::TryStreamExt;

use crate::device::wayland::BufferCollectionFile;
use crate::device::wayland::DmaNode;
use crate::errno;
use crate::error;
use crate::from_status_like_fdio;
use crate::fs::buffers::*;
use crate::fs::socket::*;
use crate::fs::FsString;
use crate::fs::*;
use crate::mode;
use crate::task::Waiter;
use crate::task::{Kernel, Task};
use crate::types::*;

/// The services that are exposed in the component's outgoing directory.
enum ExposedServices {
    ViewProvider(fuiapp::ViewProviderRequestStream),
}

/// Spawns a server that proxies wayland protocol messages between wayland clients (components run
/// in starnix) and the wayland_bridge (which translates wayland to Scenic).
pub fn serve_wayland(
    task: &Task,
    display_path: FsString,
    device_path: FsString,
) -> Result<(), Errno> {
    let display_socket = create_display_socket(task, display_path)?;

    create_device_file(task, device_path)?;

    let kernel = task.thread_group.kernel.clone();
    let outgoing_dir_channel = kernel.outgoing_dir.lock().take().ok_or(errno!(EINVAL))?;
    let kernel = Arc::downgrade(&kernel);

    let (wayland_server_channel, wayland_bridge) =
        zx::Channel::create().map_err(|_| errno!(EINVAL))?;

    // Spawn a thread that waits for the view provider from the `ViewProducer`, and then uses
    // that view provider to serve view provider requests on behalf of the component that is
    // running.
    let _view_producer_thread = std::thread::spawn(move || -> Result<(), Error> {
        let executor = fasync::LocalExecutor::new()?;
        let wayland_dispatcher = connect_to_protocol::<fvirt::WaylandDispatcherMarker>()?;
        let view_producer = connect_to_protocol::<fwayland::ViewProducerMarker>()?;

        serve_view_protocols(
            kernel,
            executor,
            display_socket,
            outgoing_dir_channel,
            wayland_server_channel,
            wayland_bridge,
            view_producer,
            wayland_dispatcher,
        )
    });

    Ok(())
}

/// Creates a wayland display socket at the provided path, using `task` to resolve the path.
fn create_display_socket(task: &Task, display_path: FsString) -> Result<SocketHandle, Errno> {
    let display_socket = Socket::new(SocketDomain::Unix, SocketType::Stream);

    let (socket_parent, socket_basename) =
        task.lookup_parent_at(FdNumber::AT_FDCWD, &display_path)?;

    let mode = task.fs.apply_umask(mode!(IFSOCK, 0o765));
    let _socket_dir_entry = socket_parent.entry.bind_socket(
        socket_basename,
        display_socket.clone(),
        SocketAddress::Unix(display_path.clone()),
        mode,
    );
    display_socket.lock().listen(1)?;

    Ok(display_socket)
}

/// Creates a memory allocation device file at the provided path.
fn create_device_file(task: &Task, device_path: FsString) -> Result<(), Errno> {
    let (device_parent, device_basename) =
        task.lookup_parent_at(FdNumber::AT_FDCWD, &device_path)?;
    let mode = task.fs.apply_umask(mode!(IFREG, 0o765));
    let _device_entry = device_parent.entry.add_node_ops(device_basename, mode, DmaNode {})?;
    Ok(())
}

/// Serves the view-related protocols on behalf of the component that is running.
///
/// This function also spawns a thread to proxy data between the wayland bridge and the display
/// socket that is exposed to the wayland client.
///
/// # Parameters
/// - `kernel`: The kernel that the server is running in.
/// - `executor`: The `fasync::Executor` to use when serving the view provider protocol.
/// - `display_socket`: The socket that exposed to the wayland client as the display socket.
/// - `outgoing_dir_channel`: The channel endpoint to serve the outgoing directory on.
/// - `wayland_server_channel`: The channel endpoint that is handed to the wayland bridge.
/// - `wayland_bridge`: The channel endpoint that this server will use to communicate with the
///                     wayland bridge.
/// - `view_producer`: The `ViewProducerProxy` which produces a `ViewProvider` that this server
///                    proxies `ViewProvider` requests to.
/// - `wayland_dispatcher`: The `WaylandDispatcherProxy` used to establish a channel connection with
///                         the wayland bridge.
fn serve_view_protocols(
    kernel: Weak<Kernel>,
    mut executor: fasync::LocalExecutor,
    display_socket: SocketHandle,
    outgoing_dir_channel: zx::Channel,
    wayland_server_channel: zx::Channel,
    wayland_bridge: zx::Channel,
    view_producer: fwayland::ViewProducerProxy,
    wayland_dispatcher: fvirt::WaylandDispatcherProxy,
) -> Result<(), Error> {
    // Inform the dispatcher of the connected client. This should be done on a per client basis,
    // but for now this assumes that only one wayland client is running in the same starnix
    // instance.
    wayland_dispatcher.on_new_connection(wayland_server_channel)?;

    // Add `ViewProvider` to the exposed services of the component, and then serve the
    // outgoing directory.
    let mut outgoing_dir = ServiceFs::new_local();
    outgoing_dir.dir("svc").add_fidl_service(ExposedServices::ViewProvider);
    outgoing_dir.serve_connection(outgoing_dir_channel)?;

    // Handle communication between the wayland client and wayland bridge.
    let _display_server_thread = std::thread::spawn(move || -> Result<(), Errno> {
        let _executor = fasync::LocalExecutor::new().map_err(|_| errno!(ENOMEM));
        serve_display_socket(kernel, display_socket, wayland_bridge)?;
        Ok(())
    });

    let mut view_producer_events = view_producer.take_event_stream();
    let view_provider = match executor.run_singlethreaded(view_producer_events.next()) {
        Some(Ok(fwayland::ViewProducerEvent::OnNewView { view_provider, id: _id })) => {
            view_provider
                .into_proxy()
                .map_err(|_| anyhow!("Error converting view provider into proxy."))
        }
        _ => Err(anyhow!("Error getting view producer's OnNewView.")),
    }?;

    executor.run_singlethreaded(serve_view_provider(view_provider, outgoing_dir));

    Ok(())
}

/// Proxies data between the `display_socket` and `wayland_bridge`.
///
/// Note that this function loops until an error is encountered, so the caller is responsible for
/// any creating any necessary threads.
fn serve_display_socket(
    kernel: Weak<Kernel>,
    display_socket: SocketHandle,
    mut wayland_bridge: zx::Channel,
) -> Result<(), Errno> {
    let waiter = Waiter::new();
    let socket = loop {
        if let Ok(socket) = display_socket.accept(ucred::default()) {
            break socket;
        }
    };

    loop {
        let kernel = match kernel.upgrade() {
            Some(kernel) => kernel,
            None => {
                log::error!("Kernel no longer alive, returning.");
                return error!(ENOMEM);
            }
        };

        send_to_wayland(&socket, &mut wayland_bridge)?;
        send_to_client(&socket, &mut wayland_bridge, &kernel)?;

        wait_async(&socket, &wayland_bridge, &waiter);

        // Drain all the packets from the port, since we just read/write in both directions on each
        // wake we don't care about the exact event that triggered the wake.
        // TODO: This should be fixed to either use a callback per direction of communication (so
        // that we only queue one packet per event), or waiter needs to be refactored to return an
        // array of events (and dequeue more than one port packet at a time).
        let mut deadline = zx::Time::INFINITE;
        loop {
            match waiter.wait_kernel(deadline) {
                Ok(_) => {}
                Err(e) if e == ETIMEDOUT => {
                    break;
                }
                err => return err,
            }
            deadline = zx::Time::ZERO;
        }
    }
}

/// Serves the `fuiapp::ViewProvider` protocol from `outgoing_dir`.
///
/// This function will continue to serve requests until `outgoing_dir.next()` errors.
async fn serve_view_provider(
    view_provider: fuiapp::ViewProviderProxy,
    mut outgoing_dir: ServiceFs<ServiceObjLocal<'static, ExposedServices>>,
) {
    // TODO: Technically it's only sensible for this to serve one view provider request stream.
    // The reason this is a loop is to make sure that we don't drop the view provider, because
    // then the wayland bridge will shut down.
    while let Some(request) = outgoing_dir.next().await {
        let mut request_stream = match request {
            ExposedServices::ViewProvider(view_provider_request_stream) => {
                view_provider_request_stream
            }
        };
        while let Ok(Some(event)) = request_stream.try_next().await {
            match event {
                fuiapp::ViewProviderRequest::CreateViewWithViewRef {
                    token,
                    mut view_ref_control,
                    mut view_ref,
                    ..
                } => {
                    match view_provider.create_view_with_view_ref(
                        token,
                        &mut view_ref_control,
                        &mut view_ref,
                    ) {
                        Ok(_) => {}
                        Err(e) => {
                            log::error!("Got an error when creating view: {:?}", e);
                            return;
                        }
                    }
                }
                r => {
                    log::warn!("Got unexpected view provider request: {:?}", r);
                }
            }
        }
    }
}

/// Sends any data that can be read out of `socket` to `wayland_channel`.
///
/// Ancillary data of type `UnixControlData::Rights` is converted to the appropriate handle before
/// sending them off to the wayland bridge.
fn send_to_wayland(socket: &Socket, wayland_channel: &mut zx::Channel) -> Result<(), Errno> {
    let messages = socket.read_kernel();

    for message in messages {
        let mut handles = vec![];
        if let Some(ancillary_data) = message.ancillary_data {
            match ancillary_data {
                AncillaryData::Unix(UnixControlData::Rights(files)) => {
                    for file in files {
                        if let Some(f) = file.downcast_file::<BufferCollectionFile>() {
                            handles.push(
                                f.token
                                    .value
                                    .as_handle_ref()
                                    .duplicate(zx::Rights::SAME_RIGHTS)
                                    .expect("Failed to duplicate buffer collection import token."),
                            );
                        }
                    }
                }
                _ => {
                    log::error!("Got unexpected ancillary data.");
                    return error!(ENOSYS);
                }
            }
        }
        wayland_channel
            .write(message.data.bytes(), &mut handles)
            .map_err(|e| from_status_like_fdio!(e))?;
    }

    Ok(())
}

/// Sends any data currently in `wayland_channel` to `socket`.
///
/// Any handles that are part of the channel message are first converted into files, prior to being
/// passed over the socket.
fn send_to_client(
    socket: &Socket,
    wayland_channel: &mut zx::Channel,
    kernel: &Kernel,
) -> Result<(), Errno> {
    let mut buffer = zx::MessageBuf::new();
    match wayland_channel.read(&mut buffer) {
        Err(zx::Status::SHOULD_WAIT) => {}
        Err(_) => {}
        Ok(_) => {
            let mut files: Vec<FileHandle> = vec![];
            while let Some(handle) = buffer.take_handle(0) {
                let vmo = zx::Vmo::from(handle);
                let file = Anon::new_file(
                    anon_fs(&kernel),
                    Box::new(VmoFileObject::new(Arc::new(vmo))),
                    OpenFlags::RDWR,
                );
                files.push(file);
            }
            let ancillary_data = if !files.is_empty() {
                Some(AncillaryData::Unix(UnixControlData::Rights(files)))
            } else {
                None
            };
            let message = Message::new(buffer.bytes().to_vec().into(), None, ancillary_data);
            socket.write_kernel(message)?;
        }
    };

    Ok(())
}

/// Registers to wait until data is available on either `socket` or `wayland_channel`.
///
/// Note that this does not actually block, that can be done by calling `waiter.wait*`.
fn wait_async(socket: &Socket, wayland_channel: &zx::Channel, waiter: &Arc<Waiter>) {
    socket.wait_async(waiter, FdEvents::POLLIN | FdEvents::POLLHUP, Box::new(|_events| {}));
    waiter
        .wake_on_signals(
            wayland_channel,
            zx::Signals::CHANNEL_READABLE | zx::Signals::CHANNEL_PEER_CLOSED,
            Box::new(|_signals| {}),
        )
        .expect("wake_on_signals failed.");
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::device::wayland::DmaFile;
    use crate::testing::create_kernel_and_task;

    /// Creates a display socket and adds it to the task's filesystem.
    ///
    /// Returns a socket that is connected to the display socket, as well as a channel endpoint that
    /// would normally be handed to the wayland bridge.
    fn create_socket_and_channel(task: &Task) -> (SocketHandle, zx::Channel) {
        let display_socket_path = b"serve_display_socket.s";
        let display_socket = create_display_socket(task, display_socket_path.to_vec())
            .expect("Failed to create display socket.");

        let (wayland_server_channel, wayland_bridge) =
            zx::Channel::create().expect("Failed to create channel");

        let weak_kernel = Arc::downgrade(&task.thread_group.kernel);
        let cloned_display_socket = display_socket.clone();
        let _display_thread = std::thread::spawn(move || {
            let _ = serve_display_socket(weak_kernel, cloned_display_socket, wayland_bridge);
        });

        let socket = Socket::new(SocketDomain::Unix, SocketType::Stream);
        socket.connect(&display_socket, ucred::default()).expect("Could not connect to socket.");

        (socket, wayland_server_channel)
    }

    /// Tests that the display socket is created as expected.
    #[test]
    fn test_create_display_socket() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let task = &task_owner.task;
        let display_socket_path = b"test.s";
        let _display_socket = create_display_socket(task, display_socket_path.to_vec())
            .expect("Failed to create display socket.");

        let node =
            task.lookup_path_from_root(display_socket_path).expect("Couldn't find socket node.");

        assert!(node.entry.node.socket().is_some());
    }

    /// Tests that the Dma file is created as expected.
    #[test]
    fn test_create_device_file() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let task = &task_owner.task;
        let device_path = b"test.d";
        create_device_file(task, device_path.to_vec()).expect("Failed to create device file.");

        let node = task.lookup_path_from_root(device_path).expect("Couldn't find device node.");
        let file = node.entry.node.open(OpenFlags::empty()).expect("Could not open device file.");

        assert!(file.as_any().downcast_ref::<DmaFile>().is_some());
    }

    /// Tests that the display socket data is proxied to the wayland bridge channel.
    #[test]
    fn test_write_display_socket() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let task = &task_owner.task;
        let (client_socket, wayland_server_channel) = create_socket_and_channel(task);

        let sent_bytes = vec![1, 2, 3];
        let message = Message::new(sent_bytes.clone().into(), None, None);
        client_socket.write_kernel(message).expect("Failed to write socket data.");

        let waiter = Waiter::new();
        waiter
            .wake_on_signals(
                &wayland_server_channel.as_handle_ref(),
                zx::Signals::CHANNEL_READABLE | zx::Signals::CHANNEL_PEER_CLOSED,
                Box::new(|_signals| {}),
            )
            .expect("wake on signals failed.");
        waiter.wait_kernel(zx::Time::INFINITE).expect("");

        let mut buffer = zx::MessageBuf::new();
        wayland_server_channel.read(&mut buffer).expect("Failed to read data from channel.");
        assert_eq!(buffer.bytes(), &sent_bytes);
    }

    /// Tests that data sent from the wayland bridge is proxied to the display socket.
    #[test]
    fn test_write_wayland_bridge() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let task = &task_owner.task;
        let (client_socket, wayland_server_channel) = create_socket_and_channel(task);

        let sent_bytes = vec![1, 2, 3];
        wayland_server_channel
            .write(&sent_bytes, &mut vec![])
            .expect("Failed to send data to channel.");

        let waiter = Waiter::new();
        client_socket.wait_async(
            &waiter,
            FdEvents::POLLIN | FdEvents::POLLHUP,
            Box::new(|_events| {}),
        );
        waiter.wait_kernel(zx::Time::INFINITE).expect("");

        let messages = client_socket.read_kernel();
        assert_eq!(messages[0].data.bytes(), sent_bytes);
    }
}
