// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::services, anyhow::Error, fidl_fuchsia_virtualization::GuestProxy,
    fuchsia_zircon_status as zx_status,
};

pub async fn handle_serial(guest: GuestProxy) -> Result<(), Error> {
    let serial = guest.get_serial().await?.map_err(zx_status::Status::from_raw)?;
    let io = services::GuestConsole::new(serial)?;
    io.run_with_stdio().await.map_err(From::from)
}

#[cfg(test)]
mod test {
    use {
        super::*,
        assert_matches::assert_matches,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_virtualization::GuestMarker,
        fuchsia_async::{self as fasync},
        futures::future::join,
        futures::StreamExt,
    };

    #[fasync::run_until_stalled(test)]
    async fn serial_get_serial_returns_error() {
        let (proxy, mut stream) = create_proxy_and_stream::<GuestMarker>().unwrap();
        let server = async move {
            let serial_responder = stream
                .next()
                .await
                .expect("Failed to read from stream")
                .expect("Failed to parse request")
                .into_get_serial()
                .expect("Unexpected call to Guest Proxy");
            serial_responder
                .send(&mut Err(zx_status::Status::INTERNAL.into_raw()))
                .expect("Failed to send request to proxy");
        };

        let client = handle_serial(proxy);

        let (_, client_res) = join(server, client).await;
        assert_matches!(client_res.unwrap_err().downcast(), Ok(zx_status::Status::INTERNAL));
    }
}
