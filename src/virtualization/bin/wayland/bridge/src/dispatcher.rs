// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        alpha_compositing::*, aura_shell::*, compositor::*, data_device_manager::*, display::*,
        linux_dmabuf::*, object::*, output::*, registry::*, seat::*, secure_output::*, shm::*,
        subcompositor::*, viewporter::*, xdg_shell::*,
    },
    anyhow::Error,
    fuchsia_zircon::{self as zx, HandleBased},
    std::io::Read,
    wayland::{WlCompositor, WlDataDeviceManager, WlOutput, WlSeat, WlShm, WlSubcompositor},
    wp_viewporter::WpViewporter,
    zaura_shell::ZauraShell,
    zcr_alpha_compositing_v1::ZcrAlphaCompositingV1,
    zcr_secure_output_v1::ZcrSecureOutputV1,
    zwp_linux_dmabuf_v1::ZwpLinuxDmabufV1,
    zxdg_shell_v6::ZxdgShellV6,
};

/// The main FIDL server that listens for incoming client connection
/// requests.
pub struct WaylandDispatcher {
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
