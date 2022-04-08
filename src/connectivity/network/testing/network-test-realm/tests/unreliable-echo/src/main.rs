// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fuchsia_net_test_realm as fntr;
use fuchsia_async as fasync;
use futures::{StreamExt as _, TryStreamExt as _};
use log::info;

#[fuchsia::main]
async fn main() {
    let result = unreliable_echo().await;
    panic!("unreliable-echo unexpectedly finished with result {:?}", result);
}

// Listens on crate::socket_addr_v{4 or 6}() depending on cmd-line args and echoes every fourth
// UDP message it receives.
async fn unreliable_echo() -> Result<(), Error> {
    info!("unreliable-echo starting");
    let args: Vec<String> = std::env::args().collect();
    let addr = match args.get(1).map(|x| &**x) {
        Some("4") => unreliable_echo::socket_addr_v4(),
        Some("6") => unreliable_echo::socket_addr_v6(),
        Some(s) => anyhow::bail!("expected args[1] to be \"4\" or \"6\", got \"{}\"", s),
        None => anyhow::bail!("expected 1 command-line argument, got none"),
    };
    let sock = fasync::net::UdpSocket::bind(&addr)?;
    let buf = vec![0; fntr::MAX_UDP_POLL_LENGTH.into()];
    {
        let sock = &sock;
        futures::stream::try_unfold((sock, buf), |(sock, mut buf)| async move {
            let (num_bytes, from_addr) = sock.recv_from(&mut buf).await?;
            Ok::<_, Error>(Some(((buf[..num_bytes].to_vec(), from_addr), (sock, buf))))
        })
        .enumerate()
        .map(|(n, val)| val.map(|(msg, from_addr)| (msg, from_addr, n)))
        .try_for_each(|(msg, from_addr, n)| async move {
            info!("unreliable-echo got message: {:?}", msg);
            if n % 4 != 3 {
                info!("dropping message because {} % 4 != 3", n);
                return Ok(());
            }
            info!("echoing");
            let num_bytes_sent = sock.send_to(&msg, from_addr).await?;
            if num_bytes_sent < msg.len() {
                Err(anyhow::anyhow!("short send ({} < {})", num_bytes_sent, msg.len()))
            } else {
                Ok(())
            }
        })
        .await
    }
}
