// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_update::{InfoRequest, InfoRequestStream},
    fuchsia_syslog::fx_log_warn,
    futures::prelude::*,
    serde_derive::{Deserialize, Serialize},
    std::fs::File,
    std::path::PathBuf,
};

#[derive(Clone)]
pub(crate) struct InfoHandler {
    pub misc_info_dir: PathBuf,
}

impl Default for InfoHandler {
    fn default() -> Self {
        Self { misc_info_dir: "/misc/ota".into() }
    }
}

impl InfoHandler {
    pub(crate) async fn handle_request_stream(
        &self,
        mut stream: InfoRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) =
            stream.try_next().await.context("extracting request from stream")?
        {
            match request {
                InfoRequest::GetChannel { responder } => {
                    let channel = self.get_channel().unwrap_or_else(|err| {
                        fx_log_warn!("error getting current channel: {}", err);
                        "".into()
                    });
                    responder.send(&channel).context("sending GetChannel response")?;
                }
            }
        }
        Ok(())
    }

    fn get_channel(&self) -> Result<String, Error> {
        // TODO: use async IO instead of sync IO once async IO is easy.
        let file = File::open(self.misc_info_dir.join("current_channel.json"))
            .context("opening current_channel.json")?;
        let contents: ChannelInfoContents =
            serde_json::from_reader(file).context("reading current_channel.json")?;
        let ChannelInfoContents::Version1(info) = contents;
        Ok(info.legacy_amber_source_name.unwrap_or_else(|| "".into()))
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
#[serde(tag = "version", content = "content", deny_unknown_fields)]
enum ChannelInfoContents {
    #[serde(rename = "1")]
    Version1(ChannelInfoV1),
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
struct ChannelInfoV1 {
    legacy_amber_source_name: Option<String>,
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_update::{InfoMarker, InfoProxy},
        fuchsia_async as fasync,
        std::fs,
        tempfile::TempDir,
    };

    fn spawn_info_handler(info_dir: &TempDir) -> InfoProxy {
        let info_handler = InfoHandler { misc_info_dir: info_dir.path().into() };
        let (proxy, stream) =
            create_proxy_and_stream::<InfoMarker>().expect("create_proxy_and_stream");
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

        let res = proxy.get_channel().await;

        assert_eq!(res.map_err(|e| e.to_string()), Ok("example".into()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_fidl_get_channel_handles_missing_file() {
        let tempdir = TempDir::new().expect("create tempdir");
        let proxy = spawn_info_handler(&tempdir);

        let res = proxy.get_channel().await;

        assert_eq!(res.map_err(|e| e.to_string()), Ok("".into()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_fidl_get_channel_handles_unexpected_contents() {
        let tempdir = TempDir::new().expect("create tempdir");
        let proxy = spawn_info_handler(&tempdir);
        fs::write(tempdir.path().join("current_channel.json"), r#"{"version":"1","content":{}}"#)
            .expect("write current_channel.json");
        let res = proxy.get_channel().await;

        assert_eq!(res.map_err(|e| e.to_string()), Ok("".into()));
    }

}
