// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use {
    crate::audio::{
        create_default_audio_stream, get_gain_db, spawn_audio_controller, spawn_audio_fidl_handler,
        DEFAULT_STREAMS,
    },
    crate::create_fidl_service,
    crate::fidl_clone::FIDLClone,
    crate::registry::base::Registry,
    crate::registry::device_storage::testing::*,
    crate::registry::registry_impl::RegistryImpl,
    crate::registry::service_context::ServiceContext,
    crate::switchboard::base::{
        AudioInfo, AudioInputInfo, AudioStream, AudioStreamType, SettingAction, SettingType,
    },
    crate::switchboard::switchboard_impl::SwitchboardImpl,
    failure::format_err,
    fidl::endpoints::{ServerEnd, ServiceMarker},
    fidl_fuchsia_media::AudioRenderUsage,
    fidl_fuchsia_settings::*,
    fuchsia_async as fasync,
    fuchsia_component::server::{ServiceFs, ServiceFsDir, ServiceObj},
    fuchsia_zircon as zx,
    futures::prelude::*,
    lazy_static::lazy_static,
    parking_lot::RwLock,
    std::collections::HashMap,
    std::sync::Arc,
};

const ENV_NAME: &str = "settings_service_audio_test_environment";

const DEFAULT_MEDIA_STREAM: AudioStream = create_default_audio_stream(AudioStreamType::Media);
const DEFAULT_SYSTEM_STREAM: AudioStream =
    create_default_audio_stream(AudioStreamType::SystemAgent);

const CHANGED_VOLUME_LEVEL: f32 = 0.7;
const CHANGED_VOLUME_MUTED: bool = true;

const CHANGED_MEDIA_STREAM: AudioStreamSettings = AudioStreamSettings {
    stream: Some(fidl_fuchsia_media::AudioRenderUsage::Media),
    source: Some(AudioStreamSettingSource::User),
    user_volume: Some(Volume {
        level: Some(CHANGED_VOLUME_LEVEL),
        muted: Some(CHANGED_VOLUME_MUTED),
    }),
};

async fn set_volume(proxy: &AudioProxy, streams: Vec<AudioStreamSettings>) {
    let mut audio_settings = AudioSettings::empty();
    audio_settings.streams = Some(streams);
    proxy.set(audio_settings).await.expect("set completed").expect("set successful");
}

// Verifies that a stream equal to |stream| is inside of |settings|.
fn verify_audio_stream(settings: AudioSettings, stream: AudioStreamSettings) {
    settings
        .streams
        .expect("audio settings contain streams")
        .into_iter()
        .find(|x| *x == stream)
        .expect("contains stream");
}

// Verifies that the |streams| map contains correct |gain_db| for |usage|.
fn verify_gain(
    gain_db: f32,
    usage: AudioRenderUsage,
    streams: &RwLock<HashMap<AudioRenderUsage, f32>>,
) {
    assert_eq!(
        gain_db,
        *(*streams.read()).get(&usage).expect("contains stream with correct gain db")
    );
}

// This function is created so that we can manipulate the |pair_media_and_system_agent| flag.
// TODO(go/fxb/37493): Remove this function and the related tests when the hack is removed.
fn create_audio_fidl_service<'a>(
    mut service_dir: ServiceFsDir<ServiceObj<'a, ()>>,
    service_context_handle: Arc<RwLock<ServiceContext>>,
    pair_media_and_system_agent: bool,
) {
    let (action_tx, action_rx) = futures::channel::mpsc::unbounded::<SettingAction>();

    // Creates switchboard, handed to interface implementations to send messages
    // to handlers.
    let (switchboard_handle, event_tx) = SwitchboardImpl::create(action_tx);

    // Creates registry, used to register handlers for setting types.
    let registry_handle = RegistryImpl::create(event_tx, action_rx);
    registry_handle
        .write()
        .register(
            SettingType::Audio,
            spawn_audio_controller(service_context_handle.clone(), pair_media_and_system_agent),
        )
        .unwrap();

    let switchboard_handle_clone = switchboard_handle.clone();
    service_dir.add_fidl_service(move |stream: AudioRequestStream| {
        spawn_audio_fidl_handler(switchboard_handle_clone.clone(), stream);
    });
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_audio() {
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
        Box::new(InMemoryStorageFactory::create()),
    );

    let env = fs.create_salted_nested_environment(ENV_NAME).unwrap();
    fasync::spawn(fs.collect());

    let audio_proxy = env.connect_to_service::<AudioMarker>().unwrap();

    let settings = audio_proxy.watch().await.expect("watch completed").expect("watch successful");
    verify_audio_stream(settings.clone(), AudioStreamSettings::from(DEFAULT_MEDIA_STREAM));

    set_volume(&audio_proxy, vec![CHANGED_MEDIA_STREAM]).await;
    let settings = audio_proxy.watch().await.expect("watch completed").expect("watch successful");
    verify_audio_stream(settings.clone(), AudioStreamSettings::from(CHANGED_MEDIA_STREAM));

    verify_gain(
        get_gain_db(CHANGED_VOLUME_LEVEL, CHANGED_VOLUME_MUTED),
        AudioRenderUsage::Media,
        &AUDIO_STREAMS,
    );
}

// Test to ensure that when |pair_media_and_system_agent| is enabled, setting the media volume
// without a system agent volume will change the system agent volume.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_audio_pair_media_system() {
    let mut changed_system_stream = CHANGED_MEDIA_STREAM.clone();
    changed_system_stream.stream = Some(fidl_fuchsia_media::AudioRenderUsage::SystemAgent);

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

    create_audio_fidl_service(
        fs.root_dir(),
        Arc::new(RwLock::new(ServiceContext::new(Some(Box::new(service_gen))))),
        true,
    );

    let env = fs.create_salted_nested_environment(ENV_NAME).unwrap();
    fasync::spawn(fs.collect());

    let audio_proxy = env.connect_to_service::<AudioMarker>().unwrap();

    let settings = audio_proxy.watch().await.expect("watch completed").expect("watch successful");
    verify_audio_stream(settings.clone(), AudioStreamSettings::from(DEFAULT_MEDIA_STREAM));
    verify_audio_stream(settings.clone(), AudioStreamSettings::from(DEFAULT_SYSTEM_STREAM));

    set_volume(&audio_proxy, vec![CHANGED_MEDIA_STREAM]).await;

    let settings = audio_proxy.watch().await.expect("watch completed").expect("watch successful");
    verify_audio_stream(settings.clone(), AudioStreamSettings::from(CHANGED_MEDIA_STREAM));
    verify_audio_stream(settings.clone(), AudioStreamSettings::from(changed_system_stream));

    let expected_gain_db = get_gain_db(CHANGED_VOLUME_LEVEL, CHANGED_VOLUME_MUTED);
    verify_gain(expected_gain_db, AudioRenderUsage::Media, &AUDIO_STREAMS);
    verify_gain(expected_gain_db, AudioRenderUsage::SystemAgent, &AUDIO_STREAMS);
}

// Test to ensure that when |pair_media_and_system_agent| is disabled, setting the media volume will
// not affect the system agent volume.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_audio_pair_media_system_off() {
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

    create_audio_fidl_service(
        fs.root_dir(),
        Arc::new(RwLock::new(ServiceContext::new(Some(Box::new(service_gen))))),
        false,
    );

    let env = fs.create_salted_nested_environment(ENV_NAME).unwrap();
    fasync::spawn(fs.collect());

    let audio_proxy = env.connect_to_service::<AudioMarker>().unwrap();

    let settings = audio_proxy.watch().await.expect("watch completed").expect("watch successful");
    verify_audio_stream(settings.clone(), AudioStreamSettings::from(DEFAULT_MEDIA_STREAM));
    verify_audio_stream(settings.clone(), AudioStreamSettings::from(DEFAULT_SYSTEM_STREAM));

    set_volume(&audio_proxy, vec![CHANGED_MEDIA_STREAM]).await;

    // The audio stream for system agent should remain the same.
    let settings = audio_proxy.watch().await.expect("watch completed").expect("watch successful");
    verify_audio_stream(settings.clone(), AudioStreamSettings::from(CHANGED_MEDIA_STREAM));
    verify_audio_stream(settings.clone(), AudioStreamSettings::from(DEFAULT_SYSTEM_STREAM));

    verify_gain(
        get_gain_db(CHANGED_VOLUME_LEVEL, CHANGED_VOLUME_MUTED),
        AudioRenderUsage::Media,
        &AUDIO_STREAMS,
    );
    assert_eq!(None, (*AUDIO_STREAMS.read()).get(&AudioRenderUsage::SystemAgent));
}

// Test to ensure that when |pair_media_and_system_agent| is enabled, setting the media volume
// with the system agent volume will not set to the system agent volume to the media volume.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_audio_pair_media_system_with_system_agent_change() {
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

    create_audio_fidl_service(
        fs.root_dir(),
        Arc::new(RwLock::new(ServiceContext::new(Some(Box::new(service_gen))))),
        true,
    );

    let env = fs.create_salted_nested_environment(ENV_NAME).unwrap();
    fasync::spawn(fs.collect());

    let audio_proxy = env.connect_to_service::<AudioMarker>().unwrap();

    let settings = audio_proxy.watch().await.expect("watch completed").expect("watch successful");
    verify_audio_stream(settings.clone(), AudioStreamSettings::from(DEFAULT_MEDIA_STREAM));
    verify_audio_stream(settings.clone(), AudioStreamSettings::from(DEFAULT_SYSTEM_STREAM));

    const CHANGED_SYSTEM_LEVEL: f32 = 0.2;
    const CHANGED_SYSTEM_MUTED: bool = false;
    const CHANGED_SYSTEM_STREAM: AudioStreamSettings = AudioStreamSettings {
        stream: Some(fidl_fuchsia_media::AudioRenderUsage::SystemAgent),
        source: Some(AudioStreamSettingSource::User),
        user_volume: Some(Volume {
            level: Some(CHANGED_SYSTEM_LEVEL),
            muted: Some(CHANGED_SYSTEM_MUTED),
        }),
    };

    set_volume(&audio_proxy, vec![CHANGED_MEDIA_STREAM, CHANGED_SYSTEM_STREAM.clone()]).await;

    let settings = audio_proxy.watch().await.expect("watch completed").expect("watch successful");
    verify_audio_stream(settings.clone(), AudioStreamSettings::from(CHANGED_MEDIA_STREAM));
    verify_audio_stream(settings.clone(), AudioStreamSettings::from(CHANGED_SYSTEM_STREAM));

    verify_gain(
        get_gain_db(CHANGED_VOLUME_LEVEL, CHANGED_VOLUME_MUTED),
        AudioRenderUsage::Media,
        &AUDIO_STREAMS,
    );

    verify_gain(
        get_gain_db(CHANGED_SYSTEM_LEVEL, CHANGED_SYSTEM_MUTED),
        AudioRenderUsage::SystemAgent,
        &AUDIO_STREAMS,
    );
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_audio_info_copy() {
    let audio_info =
        AudioInfo { streams: DEFAULT_STREAMS, input: AudioInputInfo { mic_mute: false } };

    let copy_audio_info = audio_info;
    assert_eq!(audio_info, copy_audio_info);
}
