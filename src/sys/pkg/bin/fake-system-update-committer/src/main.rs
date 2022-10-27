// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]

use anyhow::{Context as _, Error};
use fidl_fuchsia_update::{CommitStatusProviderRequest, CommitStatusProviderRequestStream};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_zircon::{self as zx, HandleBased, Peered};
use futures::prelude::*;
use std::sync::Arc;

enum IncomingServices {
    CommitStatusProvider(CommitStatusProviderRequestStream),
}

pub async fn run_system_update_committer_service(
    mut stream: CommitStatusProviderRequestStream,
    p1: Arc<zx::EventPair>,
) -> Result<(), Error> {
    while let Some(request) = stream.try_next().await.context("while obtaining request")? {
        let CommitStatusProviderRequest::IsCurrentSystemCommitted { responder, .. } = request;
        let p1_clone =
            p1.duplicate_handle(zx::Rights::BASIC).context("while duplicating event pair")?;
        let () = responder.send(p1_clone).context("while sending event pair")?;
    }

    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(IncomingServices::CommitStatusProvider);
    fs.take_and_serve_directory_handle().context("while serving directory handle")?;

    let (p0, p1) = zx::EventPair::create().context("while creating event pair")?;
    let () =
        p0.signal_peer(zx::Signals::NONE, zx::Signals::USER_0).context("while signalling peer")?;
    let p1 = Arc::new(p1);

    while let Some(service) = fs.next().await {
        match service {
            IncomingServices::CommitStatusProvider(stream) => {
                run_system_update_committer_service(stream, Arc::clone(&p1)).await?;
            }
        }
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_update::CommitStatusProviderMarker;
    use fuchsia_zircon::AsHandleRef;

    #[fasync::run_singlethreaded(test)]
    async fn fake_system_update_committer() {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<CommitStatusProviderMarker>().unwrap();

        let (p0, p1) = zx::EventPair::create().context("while creating event pair").unwrap();
        let () = p0
            .signal_peer(zx::Signals::NONE, zx::Signals::USER_0)
            .context("while signalling peer")
            .unwrap();
        let p1 = Arc::new(p1);

        let _task =
            fasync::Task::spawn(run_system_update_committer_service(stream, Arc::clone(&p1)));

        let result = proxy.is_current_system_committed().await.unwrap();
        assert_eq!(
            result.wait_handle(zx::Signals::USER_0, zx::Time::INFINITE_PAST),
            Ok(zx::Signals::USER_0)
        );
    }
}
