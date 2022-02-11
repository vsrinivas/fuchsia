// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

#[cfg(test)]
mod test {
    use {
        anyhow::Error,
        fidl_fuchsia_wayland::Server_Marker,
        fuchsia_async::{self as fasync},
        fuchsia_wayland_core::{self as wl, Interface, IntoMessage},
        fuchsia_zircon as zx,
        wayland_client_protocol::{
            WlCompositor, WlDataDeviceManager, WlDisplayRequest, WlOutput, WlSeat, WlShm,
            WlSubcompositor,
        },
        wp_viewporter_client_protocol::WpViewporter,
        xdg_shell_client_protocol::XdgWmBase,
        zaura_shell_client_protocol::ZauraShell,
        zcr_alpha_compositing_v1_client_protocol::ZcrAlphaCompositingV1,
        zcr_secure_output_v1_client_protocol::ZcrSecureOutputV1,
        zwp_linux_dmabuf_v1_client_protocol::ZwpLinuxDmabufV1,
        zwp_pointer_constraints_v1_client_protocol::ZwpPointerConstraintsV1,
        zwp_relative_pointer_v1_client_protocol::ZwpRelativePointerManagerV1,
    };

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
            "fuchsia-pkg://fuchsia.com/wayland_bridge#meta/wayland_bridge.cmx".to_string(),
            None,
        )?;
        let bridge_proxy = app.connect_to_protocol::<Server_Marker>()?;
        let (h1, h2) = zx::Channel::create()?;
        bridge_proxy.connect(h1)?;
        let client_channel = fasync::Channel::from_channel(h2)?;

        // Get the registry and issue a Sync; this should cause the server to
        // report all globals.
        let message = WlDisplayRequest::GetRegistry { registry: 0x10000 }.into_message(1)?;
        let (bytes, mut handles) = message.take();
        client_channel.write(&bytes, &mut handles)?;
        let message = WlDisplayRequest::Sync { callback: 0x10001 }.into_message(1)?;
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
            expect_global_with_name::<XdgWmBase>(6, &client_channel).await;
            expect_global_with_name::<ZwpLinuxDmabufV1>(7, &client_channel).await;
            expect_global_with_name::<ZcrAlphaCompositingV1>(8, &client_channel).await;
            expect_global_with_name::<ZcrSecureOutputV1>(9, &client_channel).await;
            expect_global_with_name::<WpViewporter>(10, &client_channel).await;
            expect_global_with_name::<ZauraShell>(11, &client_channel).await;
            expect_global_with_name::<ZwpRelativePointerManagerV1>(12, &client_channel).await;
            expect_global_with_name::<ZwpPointerConstraintsV1>(13, &client_channel).await;

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
