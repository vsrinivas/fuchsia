// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use {
    crate::audio::{create_default_audio_stream, get_gain_db},
    crate::create_fidl_service,
    crate::fidl_clone::FIDLClone,
    crate::registry::device_storage::testing::*,
    crate::registry::service_context::ServiceContext,
    crate::switchboard::base::AudioStreamType,
    crate::switchboard::base::SettingType,
    failure::format_err,
    fidl::endpoints::{ServerEnd, ServiceMarker},
    fidl_fuchsia_media::AudioRenderUsage,
    fidl_fuchsia_settings::*,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon as zx,
    futures::prelude::*,
    lazy_static::lazy_static,
    std::collections::HashMap,
    std::sync::{Arc, RwLock},
};

const ENV_NAME: &str = "settings_service_audio_test_environment";

#[fuchsia_async::run_singlethreaded(test)]
async fn test_audio() {
    let default_media_stream =
        AudioStreamSettings::from(create_default_audio_stream(AudioStreamType::Media));

    const CHANGED_VOLUME_LEVEL: f32 = 0.7;
    const CHANGED_VOLUME_MUTED: bool = true;

    let changed_media_stream = AudioStreamSettings {
        stream: Some(fidl_fuchsia_media::AudioRenderUsage::Media),
        source: Some(AudioStreamSettingSource::User),
        user_volume: Some(Volume {
            level: Some(CHANGED_VOLUME_LEVEL),
            muted: Some(CHANGED_VOLUME_MUTED),
        }),
    };

    lazy_static! {
        static ref AUDIO_STREAMS: RwLock<HashMap<AudioRenderUsage, f32>> =
            { RwLock::new(HashMap::new()) };
    }

    let service_gen = |service_name: &str, channel: zx::Channel| {
        if service_name != fidl_fuchsia_media::AudioCoreMarker::NAME {
            return Err(format_err!("unsupported!"));
        }

        let mut manager_stream =
            ServerEnd::<fidl_fuchsia_media::AudioCoreMarker>::new(channel).into_stream()?;

        fasync::spawn(async move {
            while let Some(req) = manager_stream.try_next().await.unwrap() {
                #[allow(unreachable_patterns)]
                match req {
                    fidl_fuchsia_media::AudioCoreRequest::SetRenderUsageGain {
                        usage,
                        gain_db,
                        control_handle: _,
                    } => {
                        (*AUDIO_STREAMS.write().unwrap()).insert(usage, gain_db);
                    }
                    _ => {}
                }
            }
        });

        Ok(())
    };

    let mut fs = ServiceFs::new();

    create_fidl_service(
        fs.root_dir(),
        [SettingType::Audio].iter().cloned().collect(),
        Arc::new(RwLock::new(ServiceContext::new(Some(Box::new(service_gen))))),
        Box::new(InMemoryStorageFactory::create()),
    );

    let env = fs.create_salted_nested_environment(ENV_NAME).unwrap();
    fasync::spawn(fs.collect());

    let audio_proxy = env.connect_to_service::<AudioMarker>().unwrap();

    let settings = audio_proxy.watch().await.expect("watch completed").expect("watch successful");

    let streams = settings.streams.expect("audio settings contain streams");
    streams
        .into_iter()
        .find(|x| *x == default_media_stream)
        .expect("contains default media stream");

    let mut audio_settings = AudioSettings::empty();
    audio_settings.streams = Some(vec![changed_media_stream.clone()]);

    audio_proxy.set(audio_settings).await.expect("set completed").expect("set successful");

    let settings = audio_proxy.watch().await.expect("watch completed").expect("watch successful");
    settings
        .streams
        .expect("audio settings contain streams")
        .into_iter()
        .find(|x| *x == changed_media_stream)
        .expect("contains changed media stream");

    assert_eq!(
        get_gain_db(CHANGED_VOLUME_LEVEL, CHANGED_VOLUME_MUTED),
        *(*AUDIO_STREAMS.read().unwrap())
            .get(&fidl_fuchsia_media::AudioRenderUsage::Media)
            .expect("contains media stream")
    );
}
