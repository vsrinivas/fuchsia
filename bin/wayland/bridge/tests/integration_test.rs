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
    use fuchsia_zircon as zx;
    use fuchsia_zircon::prelude::*;

    #[test]
    fn launch_bridge() -> Result<(), Error> {
        let mut exec = fasync::Executor::new()?;

        // Launch the wayland bridge process & connect to the WaylandDispatcher
        // FIDL service.
        let launcher = Launcher::new()?;
        let app = launcher.launch("wayland_bridge".to_string(), None)?;
        let bridge_proxy = app.connect_to_service(WaylandDispatcherMarker)?;
        let (h1, h2) = zx::Channel::create()?;
        bridge_proxy.on_new_connection(h1)?;

        // Receive a message on the channel. Currently this just verifies a
        // simple message is sent by the bridge but this will need to be changed
        // once the bridge is ready to process real wayland messages.
        let channel = fasync::Channel::from_channel(h2)?;
        let receiver = async move {
            let mut buffer = zx::MessageBuf::new();
            await!(channel.recv_msg(&mut buffer))
                .expect("Failed to receive message from the bridge");
            assert_eq!(1, buffer.bytes().len());
            assert_eq!(1, buffer.bytes()[0]);
            assert_eq!(0, buffer.n_handles());
        };
        let receiver = receiver.on_timeout(5.seconds().after_now(), || {
            panic!("timed out waiting for bridge response")
        });
        exec.run_singlethreaded(receiver);
        Ok(())
    }
}
