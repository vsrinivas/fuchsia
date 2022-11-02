// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// [START example]
#[cfg(test)]
mod tests {
    use anyhow::{Context as _, Error};
    use fidl_fuchsia_examples::{EchoMarker, EchoSynchronousProxy};
    use fuchsia_component::client::connect_channel_to_protocol;
    use fuchsia_zircon as zx;

    #[fuchsia::test]
    fn test() -> Result<(), Error> {
        let (server_end, client_end) = zx::Channel::create()?;
        connect_channel_to_protocol::<EchoMarker>(server_end)
            .context("Failed to connect to echo service")?;
        let echo = EchoSynchronousProxy::new(client_end);

        // Make an EchoString request, with no timeout for receiving the response.
        let res = echo.echo_string("hello", zx::Time::INFINITE)?;
        println!("response: {:?}", res);
        Ok(())
    }
}
// [END example]
