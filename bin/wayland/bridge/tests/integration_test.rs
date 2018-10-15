// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]
#![feature(async_await, await_macro, futures_api)]

#[cfg(test)]
mod test {
    use failure::Error;
    use fidl_fuchsia_guest::WaylandDispatcherMarker;
    use fuchsia_app::client::Launcher;
    use fuchsia_async::{self as fasync, TimeoutExt};
    use fuchsia_wayland_core::{self as wl, Interface};
    use fuchsia_zircon as zx;
    use fuchsia_zircon::prelude::*;
    use wayland::{WlCompositor, WlOutput, WlSeat, WlShm};

    async fn expect_global_with_name<I: Interface>(
        expect_name: u32, client_channel: &fasync::Channel,
    ) {
        let mut buffer = zx::MessageBuf::new();
        await!(client_channel.recv_msg(&mut buffer))
            .expect("Failed to receive message from the bridge");

        let mut message: wl::Message = buffer.into();
        let header = message.read_header().unwrap();
        let name = message.read_arg(wl::ArgKind::Uint).unwrap().unwrap_uint();
        let interface = message
            .read_arg(wl::ArgKind::String)
            .unwrap()
            .unwrap_string();
        let version = message.read_arg(wl::ArgKind::Uint).unwrap().unwrap_uint();
        assert_eq!(0x10000, header.sender);
        assert_eq!(0 /* wl_registry::global */, header.opcode);
        assert_eq!(expect_name, name);
        assert_eq!(I::NAME, interface);
        assert_eq!(I::VERSION, version);
    }

    #[test]
    fn list_registry() -> Result<(), Error> {
        let mut exec = fasync::Executor::new()?;

        // Launch the wayland bridge process & connect to the WaylandDispatcher
        // FIDL service.
        let launcher = Launcher::new()?;
        let app = launcher.launch("fuchsia-pkg://fuchsia.com/wayland_bridge#meta/wayland_bridge.cmx".to_string(), None)?;
        let bridge_proxy = app.connect_to_service(WaylandDispatcherMarker)?;
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
            await!(expect_global_with_name::<WlCompositor>(0, &client_channel));
            await!(expect_global_with_name::<WlOutput>(1, &client_channel));
            await!(expect_global_with_name::<WlSeat>(2, &client_channel));
            await!(expect_global_with_name::<WlShm>(3, &client_channel));

            // Expect callback::done for the sync
            let mut buffer = zx::MessageBuf::new();
            await!(client_channel.recv_msg(&mut buffer))
                .expect("Failed to receive message from the bridge");
            let mut message: wl::Message = buffer.into();
            let header = message.read_header().unwrap();
            let _callback_data = message.read_arg(wl::ArgKind::Uint).unwrap().unwrap_uint();
            assert_eq!(0x10001, header.sender);
            assert_eq!(0 /* wl_callback::done */, header.opcode);
        };
        let receiver = receiver.on_timeout(5.seconds().after_now(), || {
            panic!("timed out waiting for bridge response")
        });
        exec.run_singlethreaded(receiver);
        Ok(())
    }
}
