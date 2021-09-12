// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

#[cfg(test)]
mod test {
    use anyhow::Error;
    use fidl_fuchsia_virtualization::WaylandDispatcherMarker;
    use fuchsia_async::{self as fasync};
    use fuchsia_wayland_core::{self as wl, Interface};
    use fuchsia_zircon as zx;
    use wayland::{WlCompositor, WlDataDeviceManager, WlOutput, WlSeat, WlShm, WlSubcompositor};
    use wp_viewporter::WpViewporter;
    use zaura_shell::ZauraShell;
    use zcr_alpha_compositing_v1::ZcrAlphaCompositingV1;
    use zcr_secure_output_v1::ZcrSecureOutputV1;
    use zwp_linux_dmabuf_v1::ZwpLinuxDmabufV1;
    use zxdg_shell_v6::ZxdgShellV6;

    async fn expect_global_with_name<I: Interface>(
        expect_name: u32,
        client_channel: &fasync::Channel,
    ) {
        let mut buffer = zx::MessageBuf::new();
        client_channel
            .recv_msg(&mut buffer)
            .await
            .expect("Failed to receive message from the bridge");

        let mut message: wl::Message = buffer.into();
        let header = message.read_header().unwrap();
        let name = message.read_arg(wl::ArgKind::Uint).unwrap().unwrap_uint();
        let interface = message.read_arg(wl::ArgKind::String).unwrap().unwrap_string();
        let version = message.read_arg(wl::ArgKind::Uint).unwrap().unwrap_uint();
        assert_eq!(0x10000, header.sender);
        assert_eq!(0 /* wl_registry::global */, header.opcode);
        assert_eq!(expect_name, name);
        assert_eq!(I::NAME, interface);
        assert_eq!(I::VERSION, version);
    }

    #[test]
    fn list_registry() -> Result<(), Error> {
        let mut exec = fasync::LocalExecutor::new()?;

        // Launch the wayland bridge process & connect to the WaylandDispatcher
        // FIDL service.
        let launcher = fuchsia_component::client::launcher()?;
        let app = fuchsia_component::client::launch(
            &launcher,
            #[cfg(feature = "flatland")]
            "fuchsia-pkg://fuchsia.com/wayland_bridge#meta/wayland_bridge.cmx".to_string(),
            #[cfg(not(feature = "flatland"))]
            "fuchsia-pkg://fuchsia.com/wayland_bridge#meta/legacy_wayland_bridge.cmx".to_string(),
            None,
        )?;
        let bridge_proxy = app.connect_to_protocol::<WaylandDispatcherMarker>()?;
        let (h1, h2) = zx::Channel::create()?;
        bridge_proxy.on_new_connection(h1)?;
        let client_channel = fasync::Channel::from_channel(h2)?;

        // Send get_registry(0x10000)
        let mut message = wl::Message::new();
        message.write_header(&wl::MessageHeader {
            sender: 1,  // wl_display
            opcode: 1,  // get_registry
            length: 12, // header + new_id
        })?;
        message.write_arg(wl::Arg::NewId(0x10000))?;
        let (bytes, mut handles) = message.take();
        client_channel.write(&bytes, &mut handles)?;

        // Send sync(0x10001)
        let mut message = wl::Message::new();
        message.write_header(&wl::MessageHeader {
            sender: 1,  // wl_display
            opcode: 0,  // sync
            length: 12, // header + new_id
        })?;
        message.write_arg(wl::Arg::NewId(0x10001))?;
        let (bytes, mut handles) = message.take();
        client_channel.write(&bytes, &mut handles)?;

        // Receive a message on the channel. We expect a single global to
        // be reported right now.
        let receiver = async move {
            expect_global_with_name::<WlCompositor>(0, &client_channel).await;
            expect_global_with_name::<WlSubcompositor>(1, &client_channel).await;
            expect_global_with_name::<WlOutput>(2, &client_channel).await;
            expect_global_with_name::<WlSeat>(3, &client_channel).await;
            expect_global_with_name::<WlShm>(4, &client_channel).await;
            expect_global_with_name::<WlDataDeviceManager>(5, &client_channel).await;
            expect_global_with_name::<ZxdgShellV6>(6, &client_channel).await;
            expect_global_with_name::<ZwpLinuxDmabufV1>(7, &client_channel).await;
            expect_global_with_name::<ZcrAlphaCompositingV1>(8, &client_channel).await;
            expect_global_with_name::<ZcrSecureOutputV1>(9, &client_channel).await;
            expect_global_with_name::<WpViewporter>(10, &client_channel).await;
            expect_global_with_name::<ZauraShell>(11, &client_channel).await;

            // Expect callback::done for the sync
            let mut buffer = zx::MessageBuf::new();
            client_channel
                .recv_msg(&mut buffer)
                .await
                .expect("Failed to receive message from the bridge");
            let mut message: wl::Message = buffer.into();
            let header = message.read_header().unwrap();
            let _callback_data = message.read_arg(wl::ArgKind::Uint).unwrap().unwrap_uint();
            assert_eq!(0x10001, header.sender);
            assert_eq!(0 /* wl_callback::done */, header.opcode);
        };
        exec.run_singlethreaded(receiver);
        Ok(())
    }
}
