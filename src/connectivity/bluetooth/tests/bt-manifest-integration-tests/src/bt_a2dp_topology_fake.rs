// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::endpoints::DiscoverableProtocolMarker,
    fidl_fuchsia_bluetooth_a2dp::{AudioModeMarker, AudioModeRequestStream},
    fidl_fuchsia_bluetooth_avdtp as fidl_avdtp, fidl_fuchsia_bluetooth_avrcp as fidl_avrcp,
    fidl_fuchsia_bluetooth_bredr::ProfileMarker,
    fidl_fuchsia_bluetooth_component::LifecycleMarker,
    fidl_fuchsia_bluetooth_internal_a2dp::{ControllerMarker, ControllerRequestStream},
    fidl_fuchsia_cobalt::LoggerFactoryMarker,
    fidl_fuchsia_media::{AudioDeviceEnumeratorMarker, SessionAudioConsumerFactoryMarker},
    fidl_fuchsia_media_sessions2::PublisherMarker,
    fidl_fuchsia_mediacodec::CodecFactoryMarker,
    fidl_fuchsia_settings::AudioMarker,
    fidl_fuchsia_sysmem::AllocatorMarker,
    fidl_fuchsia_tracing_provider::RegistryMarker,
    fuchsia_async as fasync,
    fuchsia_component::{client::connect_to_protocol, server::ServiceFs},
    futures::{StreamExt, TryStream, TryStreamExt},
    log::info,
};

async fn process_request_stream<S>(mut stream: S, tag: &str)
where
    S: TryStream<Error = fidl::Error> + Unpin,
    <S as TryStream>::Ok: std::fmt::Debug,
{
    info!("Received {} service connection", tag);
    while let Some(request) = stream.try_next().await.expect("serving request stream failed") {
        info!("Received {} service request: {:?}", tag, request);
    }
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init().unwrap();
    info!("Starting bt-a2dp-topology-fake component...");

    // Set up the outgoing `svc` directory with the services A2DP provides.
    let mut fs = ServiceFs::new();
    fs.dir("svc")
        .add_fidl_service(|stream: AudioModeRequestStream| {
            fasync::Task::local(process_request_stream(stream, AudioModeMarker::PROTOCOL_NAME))
                .detach();
        })
        .add_fidl_service(|stream: fidl_avdtp::PeerManagerRequestStream| {
            fasync::Task::local(process_request_stream(
                stream,
                fidl_avdtp::PeerManagerMarker::PROTOCOL_NAME,
            ))
            .detach();
        })
        .add_fidl_service(|stream: ControllerRequestStream| {
            fasync::Task::local(process_request_stream(stream, ControllerMarker::PROTOCOL_NAME))
                .detach();
        });
    fs.take_and_serve_directory_handle().expect("Unable to serve ServiceFs requests");
    let service_fs_task = fasync::Task::spawn(fs.collect::<()>());

    // Connect to the services A2DP requires.
    let _avrcp_svc = connect_to_protocol::<fidl_avrcp::PeerManagerMarker>()?;
    let _profile_svc = connect_to_protocol::<ProfileMarker>()?;
    let _cobalt_svc = connect_to_protocol::<LoggerFactoryMarker>()?;
    let _audio_device_svc = connect_to_protocol::<AudioDeviceEnumeratorMarker>()?;
    let _session_audio_svc = connect_to_protocol::<SessionAudioConsumerFactoryMarker>()?;
    let _publisher_svc = connect_to_protocol::<PublisherMarker>()?;
    let _codec_factory_svc = connect_to_protocol::<CodecFactoryMarker>()?;
    let _audio_svc = connect_to_protocol::<AudioMarker>()?;
    let _allocator_svc = connect_to_protocol::<AllocatorMarker>()?;
    let _tracing_svc = connect_to_protocol::<RegistryMarker>()?;
    // A2DP also relies on the Lifecycle service which is provided by its child `bt-avrcp-target`.
    let _lifecycle_svc = connect_to_protocol::<LifecycleMarker>()?;

    service_fs_task.await;
    Ok(())
}
