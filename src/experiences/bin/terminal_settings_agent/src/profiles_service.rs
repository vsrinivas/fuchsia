// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_terminal::{ProfilesRequest, ProfilesRequestStream},
    fuchsia_syslog::fx_log_info,
    futures::prelude::*,
};

/// Creates a fidl server which handles the incoming stream.
///
/// This method will run until the proxy closes the connection.
pub async fn run_fidl_server(mut stream: ProfilesRequestStream) -> Result<(), Error> {
    while let Some(req) = stream.try_next().await? {
        // TODO:(41124) This is just a stub implementation for now. Support
        // needs to be added for all events.
        match req {
            ProfilesRequest::GetProfileList { responder } => {
                let list = Vec::new();
                responder.send(&mut list.into_iter())?;
            }
            _ => fx_log_info!("unsupported event called!"),
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_terminal::ProfilesMarker,
        fuchsia_async::{self as fasync, DurationExt},
        fuchsia_zircon as zx,
    };

    #[fasync::run_singlethreaded(test)]
    async fn shuts_down_when_proxy_closes() -> Result<(), Error> {
        let (proxy, stream) = create_proxy_and_stream::<ProfilesMarker>()?;
        fasync::spawn(async move {
            let timeout = zx::Duration::from_nanos(100).after_now();
            fasync::Timer::new(timeout).await;
            drop(proxy);
        });
        run_fidl_server(stream)
            .await
            .unwrap_or_else(|e| panic!("Error while serving profiles service: {}", e));

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn can_get_profiles_list() -> Result<(), Error> {
        let (proxy, stream) = create_proxy_and_stream::<ProfilesMarker>()?;
        fasync::spawn(
            run_fidl_server(stream)
                .unwrap_or_else(|e| panic!("Error while serving profiles service: {}", e)),
        );

        let _list = proxy.get_profile_list().await?;

        Ok(())
    }
}
