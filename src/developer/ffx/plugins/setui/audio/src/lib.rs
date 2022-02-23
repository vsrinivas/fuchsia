// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use ffx_core::ffx_plugin;
use ffx_setui_audio_args::Audio;
use fidl_fuchsia_settings::{AudioProxy, AudioSettings};
use utils::handle_mixed_result;
use utils::{self, Either, WatchOrSetResult};

#[ffx_plugin("setui", AudioProxy = "core/setui_service:expose:fuchsia.settings.Audio")]
pub async fn run_command(audio_proxy: AudioProxy, audio: Audio) -> Result<()> {
    handle_mixed_result("Audio", command(audio_proxy, audio).await).await
}

async fn command(proxy: AudioProxy, audio: Audio) -> WatchOrSetResult {
    let settings = AudioSettings::from(audio);

    if settings == AudioSettings::EMPTY {
        Ok(Either::Watch(utils::watch_to_stream(proxy, |p| p.watch())))
    } else {
        Ok(Either::Set(if let Err(err) = proxy.set(settings.clone()).await? {
            format!("{:?}", err)
        } else {
            format!("Successfully set Audio to {:?}", audio)
        }))
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fidl_fuchsia_media::AudioRenderUsage;
    use fidl_fuchsia_settings::AudioRequest;
    use futures::prelude::*;
    use test_case::test_case;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_run_command() {
        let proxy = setup_fake_audio_proxy(move |req| match req {
            AudioRequest::Set { responder, .. } => {
                let _ = responder.send(&mut Ok(()));
            }
            AudioRequest::Watch { .. } => {
                panic!("Unexpected call to watch");
            }
        });

        let audio = Audio {
            stream: None,
            source: None,
            level: Some(0.5),
            volume_muted: Some(false),
            input_muted: Some(false),
        };
        let response = run_command(proxy, audio).await;
        assert!(response.is_ok());
    }

    #[test_case(
        Audio {
            stream: Some(AudioRenderUsage::Media),
            source: Some(fidl_fuchsia_settings::AudioStreamSettingSource::User),
            level: Some(0.6),
            volume_muted: Some(false),
            input_muted: Some(false),
        };
        "Test audio set() output with non-empty input."
    )]
    #[test_case(
        Audio {
            stream: Some(AudioRenderUsage::Background),
            source: Some(fidl_fuchsia_settings::AudioStreamSettingSource::User),
            level: Some(0.1),
            volume_muted: Some(false),
            input_muted: Some(true),
        };
        "Test audio set() output with a different non-empty input."
    )]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn validate_do_not_disturb_set_output(expected_audio: Audio) -> Result<()> {
        let proxy = setup_fake_audio_proxy(move |req| match req {
            AudioRequest::Set { responder, .. } => {
                let _ = responder.send(&mut Ok(()));
            }
            AudioRequest::Watch { .. } => {
                panic!("Unexpected call to watch");
            }
        });

        let output = utils::assert_set!(command(proxy, expected_audio));
        assert_eq!(output, format!("Successfully set Audio to {:?}", expected_audio));
        Ok(())
    }

    #[test_case(
        Audio {
            stream: None,
            source: None,
            level: None,
            volume_muted: None,
            input_muted: None,
        };
        "Test audio watch() output with empty input."
    )]
    #[test_case(
        Audio {
            stream: Some(AudioRenderUsage::Background),
            source: Some(fidl_fuchsia_settings::AudioStreamSettingSource::User),
            level: Some(0.1),
            volume_muted: Some(false),
            input_muted: Some(true),
        };
        "Test audio watch() output with non-empty input."
    )]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn validate_do_not_disturb_watch_output(expected_audio: Audio) -> Result<()> {
        let proxy = setup_fake_audio_proxy(move |req| match req {
            AudioRequest::Set { .. } => {
                panic!("Unexpected call to set");
            }
            AudioRequest::Watch { responder } => {
                let _ = responder.send(AudioSettings::from(expected_audio));
            }
        });

        let output = utils::assert_watch!(command(
            proxy,
            Audio {
                stream: None,
                source: None,
                level: None,
                volume_muted: None,
                input_muted: None
            }
        ));
        assert_eq!(output, format!("{:#?}", AudioSettings::from(expected_audio)));
        Ok(())
    }
}
