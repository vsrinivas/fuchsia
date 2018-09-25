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
    use wayland::WlCompositor;

    #[test]
    fn list_registry() -> Result<(), Error> {
        let mut exec = fasync::Executor::new()?;

        // Launch the wayland bridge process & connect to the WaylandDispatcher
        // FIDL service.
        let launcher = Launcher::new()?;
        let app = launcher.launch("wayland_bridge".to_string(), None)?;
        let bridge_proxy = app.connect_to_service(WaylandDispatcherMarker)?;
        let (h1, h2) = zx::Channel::create()?;
        bridge_proxy.on_new_connection(h1)?;
        let client_channel = fasync::Channel::from_channel(h2)?;

        // Send get_registry(0x10000)
        let mut message = wl::Message::new();
        message.write_header(&wl::MessageHeader {
            sender: 1, // wl_display
            opcode: 1, // get_registry
            length: 12, // header + new_id
        })?;
        message.write_arg(wl::Arg::Uint(0x10000))?;
        let (bytes, mut handles) = message.take();
        client_channel.write(&bytes, &mut handles)?;

        // Receive a message on the channel. We expect a single global to
        // be reported right now.
        //
        // TODO(tjdetwiler): once wl_display::sync is wired up we can make this
        // more smarter.
        let receiver = async move {
            let mut buffer = zx::MessageBuf::new();
            await!(client_channel.recv_msg(&mut buffer))
                .expect("Failed to receive message from the bridge");
            let mut message: wl::Message = buffer.into();
            let header = message.read_header().unwrap();
            assert_eq!(0x10000, header.sender);
            assert_eq!(0 /* wl_registry::global */, header.opcode);

            let name = message.read_arg(wl::ArgKind::Uint).unwrap().unwrap_uint();
            let interface = message.read_arg(wl::ArgKind::String).unwrap().unwrap_string();
            let version = message.read_arg(wl::ArgKind::Uint).unwrap().unwrap_uint();
            assert_eq!(0, name);
            assert_eq!(WlCompositor::NAME, interface);
            assert_eq!(0, version);
        };
        let receiver = receiver.on_timeout(5.seconds().after_now(), || {
            panic!("timed out waiting for bridge response")
        });
        exec.run_singlethreaded(receiver);
        Ok(())
    }
}
