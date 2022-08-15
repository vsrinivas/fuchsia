// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_posix as fposix;
use fidl_fuchsia_posix_socket_packet as fppacket;
use futures::{future, TryStreamExt as _};

pub(crate) async fn serve(stream: fppacket::ProviderRequestStream) -> Result<(), fidl::Error> {
    stream
        .try_for_each(|req| {
            match req {
                fppacket::ProviderRequest::Socket { responder, kind: _ } => {
                    log::error!("TODO(https://fxbug.dev/106735): Support packet sockets");
                    responder_send!(responder, &mut Err(fposix::Errno::Enoprotoopt));
                }
            };
            future::ok(())
        })
        .await
}
