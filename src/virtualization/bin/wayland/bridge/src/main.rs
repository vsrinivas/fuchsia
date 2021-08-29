// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "128"]

use {
    anyhow::Error,
    fidl_fuchsia_virtualization::{WaylandDispatcherRequest, WaylandDispatcherRequestStream},
    fidl_fuchsia_wayland::ViewProducerRequestStream,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_trace_provider::trace_provider_create_with_fdio,
    fuchsia_zircon::{self as zx, HandleBased},
    futures::prelude::*,
    std::io::Read,
    wayland::{WlCompositor, WlDataDeviceManager, WlOutput, WlSeat, WlShm, WlSubcompositor},
    wp_viewporter::WpViewporter,
    zaura_shell::ZauraShell,
    zcr_alpha_compositing_v1::ZcrAlphaCompositingV1,
    zcr_secure_output_v1::ZcrSecureOutputV1,
    zwp_linux_dmabuf_v1::ZwpLinuxDmabufV1,
    zxdg_shell_v6::ZxdgShellV6,
};

mod client;
mod object;
use crate::object::*;
mod registry;
use crate::registry::*;
mod alpha_compositing;
use crate::alpha_compositing::*;
mod aura_shell;
use crate::aura_shell::*;
mod compositor;
use crate::compositor::*;
mod data_device_manager;
use crate::data_device_manager::*;
mod display;
use crate::display::*;
mod output;
use crate::output::*;
mod secure_output;
use crate::secure_output::*;
mod scenic;
mod seat;
use crate::seat::*;
mod shm;
use crate::shm::*;
mod subcompositor;
use crate::subcompositor::*;
mod viewporter;
use crate::viewporter::*;
mod xdg_shell;
use crate::xdg_shell::*;
mod linux_dmabuf;
use crate::linux_dmabuf::*;
mod buffer;

#[cfg(test)]
mod test_protocol;

/// The main FIDL server that listens for incoming client connection
/// requests.
struct WaylandDispatcher {
    /// The display handles the creation of new clients. Must be
    /// Arc/Mutex since this is shared with the future run on the executor.
    pub display: Display,
}

impl WaylandDispatcher {
    pub fn new() -> Result<Self, Error> {
        let mut registry = RegistryBuilder::new();
        registry.add_global(WlCompositor, move |_, _, _| {
            Ok(Box::new(RequestDispatcher::new(Compositor::new())))
        });
        registry.add_global(WlSubcompositor, move |_, _, _| {
            Ok(Box::new(RequestDispatcher::new(Subcompositor::new())))
        });
        registry.add_global(WlOutput, move |id, _, client| {
            Output::update_display_info(id, client);
            Ok(Box::new(RequestDispatcher::new(Output::new())))
        });
        {
            let mut keymap_file = std::fs::File::open("/pkg/data/keymap.xkb")?;
            let keymap_len = keymap_file.metadata()?.len();

            let mut buffer = Vec::new();
            keymap_file.read_to_end(&mut buffer)?;
            let keymap_vmo = zx::Vmo::create(keymap_len)?;
            keymap_vmo.write(&buffer, 0)?;

            registry.add_global(WlSeat, move |id, version, client| {
                let seat = Seat::new(
                    version,
                    keymap_vmo.duplicate_handle(zx::Rights::SAME_RIGHTS)?,
                    keymap_len as u32,
                );
                seat.post_seat_info(id, client)?;
                Ok(Box::new(RequestDispatcher::new(seat)))
            });
        }
        registry.add_global(WlShm, move |id, _, client| {
            let shm = Shm::new();
            // announce the set of supported shm pixel formats.
            shm.post_formats(id, client)?;
            Ok(Box::new(RequestDispatcher::new(shm)))
        });
        registry.add_global(WlDataDeviceManager, move |_, _, _| {
            Ok(Box::new(RequestDispatcher::new(DataDeviceManager::new())))
        });
        registry.add_global(ZxdgShellV6, move |_, _, _| {
            let xdg_shell = XdgShell::new();
            Ok(Box::new(RequestDispatcher::new(xdg_shell)))
        });
        registry.add_global(ZwpLinuxDmabufV1, move |id, _, client| {
            let linux_dmabuf = LinuxDmabuf::new();
            // announce the set of supported pixel formats.
            linux_dmabuf.post_formats(id, client)?;
            Ok(Box::new(RequestDispatcher::new(linux_dmabuf)))
        });
        registry.add_global(ZcrAlphaCompositingV1, move |_, _, _| {
            Ok(Box::new(RequestDispatcher::new(AlphaCompositing::new())))
        });
        registry.add_global(ZcrSecureOutputV1, move |_, _, _| {
            Ok(Box::new(RequestDispatcher::new(SecureOutput::new())))
        });
        registry.add_global(WpViewporter, move |_, _, _| {
            Ok(Box::new(RequestDispatcher::new(Viewporter::new())))
        });
        registry.add_global(ZauraShell, move |_, _, _| {
            Ok(Box::new(RequestDispatcher::new(AuraShell::new())))
        });
        let display = Display::new(registry.build())?;
        Ok(WaylandDispatcher { display })
    }
}

fn spawn_wayland_dispatcher_service(mut stream: WaylandDispatcherRequestStream, display: Display) {
    fasync::Task::local(
        async move {
            while let Some(WaylandDispatcherRequest::OnNewConnection { channel, .. }) =
                stream.try_next().await.unwrap()
            {
                display.clone().spawn_new_client(fasync::Channel::from_channel(channel).unwrap());
            }
            Ok(())
        }
        .unwrap_or_else(|e: anyhow::Error| println!("{:?}", e)),
    )
    .detach();
}

fn spawn_view_producer_service(stream: ViewProducerRequestStream, display: Display) {
    display.bind_view_producer(stream);
}

fn main() -> Result<(), Error> {
    trace_provider_create_with_fdio();
    let mut exec = fasync::LocalExecutor::new()?;
    let dispatcher = WaylandDispatcher::new()?;
    let display = &dispatcher.display;
    let mut fs = ServiceFs::new();
    fs.dir("svc")
        .add_fidl_service(|stream| spawn_wayland_dispatcher_service(stream, display.clone()))
        .add_fidl_service(|stream| spawn_view_producer_service(stream, display.clone()));
    fs.take_and_serve_directory_handle().unwrap();
    exec.run_singlethreaded(fs.collect::<()>());
    Ok(())
}
