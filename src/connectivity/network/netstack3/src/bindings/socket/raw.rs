// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_posix as fposix;
use fidl_fuchsia_posix_socket_raw as fpraw;
use futures::{future, TryStreamExt as _};

pub(crate) async fn serve(stream: fpraw::ProviderRequestStream) -> Result<(), fidl::Error> {
    stream
        .try_for_each(|req| {
            match req {
                fpraw::ProviderRequest::Socket { responder, domain: _, proto: _ } => {
                    log::error!("TODO(https://fxbug.dev/106736): Support raw sockets");
                    responder_send!(responder, &mut Err(fposix::Errno::Enoprotoopt));
                }
            };
            future::ok(())
        })
        .await
}
