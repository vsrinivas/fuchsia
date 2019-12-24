// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::rate_limiter::RateLimiterMonotonic,
    anyhow::{Context as _, Error},
    fidl_fuchsia_update_channel::{ProviderRequest, ProviderRequestStream},
    fuchsia_syslog::fx_log_warn,
    fuchsia_zircon as zx,
    futures::prelude::*,
    serde_derive::{Deserialize, Serialize},
    std::{fs::File, io, path::PathBuf},
};

pub(crate) struct ProviderHandler {
    misc_info_dir: PathBuf,
    warn_rate_limiter: RateLimiterMonotonic,
}

impl Default for ProviderHandler {
    fn default() -> Self {
        Self {
            misc_info_dir: "/misc/ota".into(),
            warn_rate_limiter: RateLimiterMonotonic::from_delay(GET_CURRENT_WARN_DELAY),
        }
    }
}

const GET_CURRENT_WARN_DELAY: zx::Duration = zx::Duration::from_minutes(30);

impl ProviderHandler {
    pub(crate) async fn handle_request_stream(
        &self,
        mut stream: ProviderRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) =
            stream.try_next().await.context("extracting request from stream")?
        {
            match request {
                ProviderRequest::GetCurrent { responder } => {
                    let channel = self.get_current().unwrap_or_else(|err| {
                        self.warn_rate_limiter.rate_limit(|| {
                            fx_log_warn!("error getting current channel: {}", err);
                        });
                        "".into()
                    });
                    responder.send(&channel).context("sending GetCurrent response")?;
                }
            }
        }
        Ok(())
    }

    fn get_current(&self) -> Result<String, Error> {
        // TODO: use async IO instead of sync IO once async IO is easy.
        let file = File::open(self.misc_info_dir.join("current_channel.json"))
            .context("opening current_channel.json")?;
        let contents: ChannelProviderContents = serde_json::from_reader(io::BufReader::new(file))
            .context("reading current_channel.json")?;
        let ChannelProviderContents::Version1(info) = contents;
        Ok(info.legacy_amber_source_name.unwrap_or_else(|| "".into()))
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
#[serde(tag = "version", content = "content", deny_unknown_fields)]
enum ChannelProviderContents {
    #[serde(rename = "1")]
    Version1(ChannelProviderV1),
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
struct ChannelProviderV1 {
    legacy_amber_source_name: Option<String>,
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_update_channel::{ProviderMarker, ProviderProxy},
        fuchsia_async as fasync,
        std::fs,
        tempfile::TempDir,
    };

    fn spawn_info_handler(info_dir: &TempDir) -> ProviderProxy {
        let info_handler = ProviderHandler {
            misc_info_dir: info_dir.path().into(),
            warn_rate_limiter: RateLimiterMonotonic::from_delay(GET_CURRENT_WARN_DELAY),
        };
        let (proxy, stream) =
            create_proxy_and_stream::<ProviderMarker>().expect("create_proxy_and_stream");
        fasync::spawn(async move { info_handler.handle_request_stream(stream).map(|_| ()).await });
        proxy
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_fidl_get_channel_works() {
        let tempdir = TempDir::new().expect("create tempdir");
        fs::write(
            tempdir.path().join("current_channel.json"),
            r#"{"version":"1","content":{"legacy_amber_source_name":"example"}}"#,
        )
        .expect("write current_channel.json");
        let proxy = spawn_info_handler(&tempdir);

        let res = proxy.get_current().await;

        assert_eq!(res.map_err(|e| e.to_string()), Ok("example".into()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_fidl_get_channel_handles_missing_file() {
        let tempdir = TempDir::new().expect("create tempdir");
        let proxy = spawn_info_handler(&tempdir);

        let res = proxy.get_current().await;

        assert_eq!(res.map_err(|e| e.to_string()), Ok("".into()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_fidl_get_channel_handles_unexpected_contents() {
        let tempdir = TempDir::new().expect("create tempdir");
        let proxy = spawn_info_handler(&tempdir);
        fs::write(tempdir.path().join("current_channel.json"), r#"{"version":"1","content":{}}"#)
            .expect("write current_channel.json");
        let res = proxy.get_current().await;

        assert_eq!(res.map_err(|e| e.to_string()), Ok("".into()));
    }
}
