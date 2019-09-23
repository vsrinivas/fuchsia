// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use {
    crate::audio::{create_default_audio_stream, get_gain_db, DEFAULT_AUDIO_INFO},
    crate::create_fidl_service,
    crate::registry::device_storage::testing::*,
    crate::registry::device_storage::DeviceStorageFactory,
    crate::registry::service_context::ServiceContext,
    crate::switchboard::base::SettingType,
    crate::switchboard::base::{AudioInfo, AudioSettingSource, AudioStream, AudioStreamType},
    failure::format_err,
    fidl::endpoints::{ServerEnd, ServiceMarker},
    fidl_fuchsia_media::AudioRenderUsage,
    fidl_fuchsia_settings::*,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon as zx,
    futures::prelude::*,
    lazy_static::lazy_static,
    parking_lot::RwLock,
    std::collections::HashMap,
    std::sync::Arc,
};

const ENV_NAME: &str = "settings_service_audio_test_environment";

#[fuchsia_async::run_singlethreaded(test)]
async fn test_audio() {
    // Populate the initial value.
    let storage_factory = Box::new(InMemoryStorageFactory::create());
    let store = storage_factory.get_store::<AudioInfo>();
    {
        let mut store_lock = store.lock().await;
        store_lock.write(DEFAULT_AUDIO_INFO, false).await.unwrap();
    }

    let default_media_stream =
        AudioStreamSettings::from(create_default_audio_stream(AudioStreamType::Media));

    const CHANGED_VOLUME_LEVEL: f32 = 0.7;
    const CHANGED_VOLUME_MUTED: bool = true;

    let changed_media_stream = AudioStream {
        stream_type: AudioStreamType::Media,
        source: AudioSettingSource::User,
        user_volume_level: CHANGED_VOLUME_LEVEL,
        user_volume_muted: CHANGED_VOLUME_MUTED,
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
                        (*AUDIO_STREAMS.write()).insert(usage, gain_db);
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
        storage_factory,
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
    audio_settings.streams = Some(vec![AudioStreamSettings::from(changed_media_stream)]);

    audio_proxy.set(audio_settings).await.expect("set completed").expect("set successful");

    let settings = audio_proxy.watch().await.expect("watch completed").expect("watch successful");
    settings
        .streams
        .expect("audio settings contain streams")
        .into_iter()
        .find(|x| *x == AudioStreamSettings::from(changed_media_stream))
        .expect("contains changed media stream");

    assert_eq!(
        get_gain_db(CHANGED_VOLUME_LEVEL, CHANGED_VOLUME_MUTED),
        *(*AUDIO_STREAMS.read())
            .get(&fidl_fuchsia_media::AudioRenderUsage::Media)
            .expect("contains media stream")
    );

    // Check to make sure value wrote out to store correctly.
    {
        let mut store_lock = store.lock().await;
        store_lock
            .get()
            .await
            .streams
            .into_iter()
            .find(|x| *x == &changed_media_stream)
            .expect("contains changed media stream");
    }
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_audio_info_copy() {
    let audio_info = DEFAULT_AUDIO_INFO;
    let copy_audio_info = audio_info;
    assert_eq!(audio_info, copy_audio_info);
}
