// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        alpha_compositing::*, aura_shell::*, compositor::*, data_device_manager::*, display::*,
        linux_dmabuf::*, object::*, output::*, pointer_constraints::*, registry::*,
        relative_pointer::*, seat::*, secure_output::*, shm::*, subcompositor::*, viewporter::*,
        xdg_shell::*,
    },
    anyhow::Error,
    fuchsia_zircon::{self as zx, HandleBased},
    parking_lot::Mutex,
    std::io::Read,
    std::sync::Arc,
    wayland_server_protocol::{
        WlCompositor, WlDataDeviceManager, WlOutput, WlSeat, WlShm, WlSubcompositor,
    },
    wp_viewporter_server_protocol::WpViewporter,
    xdg_shell_server_protocol::XdgWmBase,
    zaura_shell_server_protocol::ZauraShell,
    zcr_alpha_compositing_v1_server_protocol::ZcrAlphaCompositingV1,
    zcr_secure_output_v1_server_protocol::ZcrSecureOutputV1,
    zwp_linux_dmabuf_v1_server_protocol::ZwpLinuxDmabufV1,
    zwp_pointer_constraints_v1_server_protocol::ZwpPointerConstraintsV1,
    zwp_relative_pointer_v1_server_protocol::ZwpRelativePointerManagerV1,
};

/// The main FIDL server that listens for incoming client connection
/// requests.
pub struct WaylandDispatcher {
    /// The display handles the creation of new clients. Must be
    /// Arc/Mutex since this is shared with the future run on the executor.
    pub display: Display,
}

impl WaylandDispatcher {
    pub fn new_local(client: Arc<Mutex<Box<dyn LocalViewProducerClient>>>) -> Result<Self, Error> {
        let registry = WaylandDispatcher::new_registry()?;
        let display = Display::new_local(registry, client)?;
        Ok(WaylandDispatcher { display })
    }

    pub fn new() -> Result<Self, Error> {
        let registry = WaylandDispatcher::new_registry()?;
        let display = Display::new(registry)?;
        Ok(WaylandDispatcher { display })
    }

    fn new_registry() -> Result<Registry, Error> {
        let mut registry = RegistryBuilder::new();
        registry.add_global(WlCompositor, move |_, _, _| {
            Ok(Box::new(RequestDispatcher::new(Compositor::new())))
        });
        registry.add_global(WlSubcompositor, move |_, _, _| {
            Ok(Box::new(RequestDispatcher::new(Subcompositor::new())))
        });
        registry.add_global(WlOutput, move |id, _, client| {
            let output = Output::new();
            let display_info = client.display().display_info();
            // Send display info.
            Output::post_output_info(id, client, &display_info)?;
            Output::post_output_done(id, client)?;
            Ok(Box::new(RequestDispatcher::new(output)))
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
                seat.post_seat_info(id, version, client)?;
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
        registry.add_global(XdgWmBase, move |_, _, _| {
            let xdg_shell = XdgShell::new();
            Ok(Box::new(RequestDispatcher::new(xdg_shell)))
        });
        registry.add_global(ZwpLinuxDmabufV1, move |id, version, client| {
            let linux_dmabuf = LinuxDmabuf::new(version);
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
        registry.add_global(ZwpRelativePointerManagerV1, move |_, _, _| {
            Ok(Box::new(RequestDispatcher::new(RelativePointerManager)))
        });
        registry.add_global(ZwpPointerConstraintsV1, move |_, _, _| {
            Ok(Box::new(RequestDispatcher::new(PointerConstraints)))
        });

        Ok(registry.build())
    }
}
