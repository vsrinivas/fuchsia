// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use serde_derive::Deserialize;
use std::fs;
use std::path::Path;

#[derive(Deserialize)]
#[serde(tag = "version", content = "content", deny_unknown_fields)]
enum Channel {
    #[serde(rename = "1")]
    Version1 { legacy_amber_source_name: String },
}

pub fn read_channel(path: impl AsRef<Path>) -> Result<String, Error> {
    let f = fs::File::open(path.as_ref())?;
    match serde_json::from_reader(f)? {
        Channel::Version1 { legacy_amber_source_name } => Ok(legacy_amber_source_name),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_update::ChannelControlMarker;
    use fuchsia_async as fasync;
    use fuchsia_component::client::connect_to_service;

    #[fasync::run_singlethreaded(test)]
    async fn test_fake_channel_control() {
        let control = connect_to_service::<ChannelControlMarker>().unwrap();

        control.set_target("test-target-channel").await.unwrap();
        assert_eq!("test-target-channel", control.get_target().await.unwrap());
        assert_eq!("fake-current-channel", control.get_current().await.unwrap());

        control.set_target("test-target-channel-2").await.unwrap();
        assert_eq!("test-target-channel-2", control.get_target().await.unwrap());
        assert_eq!("fake-current-channel", control.get_current().await.unwrap());
    }

    #[test]
    fn test_read_channel() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("current_channel.json");

        fs::write(&path, r#"{"version":"1","content":{"legacy_amber_source_name":"stable"}}"#)
            .unwrap();
        let channel = read_channel(path).unwrap();
        assert_eq!("stable", channel);
    }
}
