// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::spawn_log_error;
use crate::Result;
use failure::ResultExt;
use fidl_fuchsia_media::*;
use fidl_fuchsia_media_sounds::*;
use fuchsia_async as fasync;
use fuchsia_component::client::{launch, App};
use fuchsia_component::server::*;
use fuchsia_zircon::{self as zx, prelude::AsHandleRef};
use futures::{channel::oneshot, StreamExt};

const SOUNDPLAYER_URL: &str = "fuchsia-pkg://fuchsia.com/soundplayer#meta/soundplayer.cmx";
const PAYLOAD_SIZE: usize = 1024;
const USAGE: AudioRenderUsage = AudioRenderUsage::Media;

#[fasync::run_singlethreaded]
#[test]
async fn integration() -> Result<()> {
    let (sender, receiver) = oneshot::channel::<()>();
    let (mut buffer, koid, mut stream_type) = test_sound(PAYLOAD_SIZE);

    let service = TestService::new(
        sender,
        RendererExpectations {
            vmo_koid: koid,
            packet: StreamPacket {
                pts: 0,
                payload_buffer_id: 0,
                payload_offset: 0,
                payload_size: PAYLOAD_SIZE as u64,
                flags: 0,
                buffer_config: 0,
                stream_segment_id: 0,
            },
            stream_type: stream_type.clone(),
            usage: USAGE,
        },
    );

    service
        .sound_player
        .add_sound_buffer(0, &mut buffer, &mut stream_type)
        .expect("Calling add_sound");
    service
        .sound_player
        .play_sound(0, USAGE)
        .await
        .context("Calling play_sound")?
        .map_err(|err| failure::format_err!("Error playing sound: {:?}", err))?;
    service.sound_player.remove_sound(0).expect("Calling remove_sound");

    receiver.await.map_err(|_| failure::format_err!("Error awaiting test completion"))
}

struct TestService {
    #[allow(unused)]
    app: App, // This needs to stay alive to keep the service running.
    #[allow(unused)]
    environment: NestedEnvironment, // This needs to stay alive to keep the service running.
    sound_player: PlayerProxy,
}

impl TestService {
    fn new(sender: oneshot::Sender<()>, renderer_expectations: RendererExpectations) -> Self {
        let mut service_fs = ServiceFs::new();
        let mut sender_option = Some(sender);

        service_fs
            .add_fidl_service(move |request_stream: AudioRequestStream| {
                spawn_log_error(
                    FakeAudio::new(
                        sender_option.take().expect("Audio service created twice."),
                        renderer_expectations.clone(),
                    )
                    .serve(request_stream),
                );
            })
            .add_proxy_service::<fidl_fuchsia_logger::LogSinkMarker, ()>();

        let environment = service_fs
            .create_nested_environment("soundplayer-integration-test")
            .expect("Creating nested environment");
        let app = launch(environment.launcher(), String::from(SOUNDPLAYER_URL), None)
            .expect("Launching soundplayer");
        let sound_player = app.connect_to_service::<PlayerMarker>().expect("Connecting to Player");

        fasync::spawn_local(service_fs.collect::<()>());

        Self { app, environment, sound_player }
    }
}

fn test_sound(size: usize) -> (fidl_fuchsia_mem::Buffer, zx::Koid, AudioStreamType) {
    let vmo = zx::Vmo::create(size as u64).expect("Creating VMO");
    let koid = vmo.get_koid().expect("Getting vmo koid");
    let stream_type = AudioStreamType {
        sample_format: AudioSampleFormat::Signed16,
        channels: 1,
        frames_per_second: 44100,
    };

    (fidl_fuchsia_mem::Buffer { vmo: vmo, size: size as u64 }, koid, stream_type)
}

#[derive(Copy, Clone)]
struct RendererExpectations {
    vmo_koid: zx::Koid,
    packet: StreamPacket,
    stream_type: AudioStreamType,
    usage: AudioRenderUsage,
}

struct FakeAudio {
    renderer_expectations: RendererExpectations,
    sender: oneshot::Sender<()>,
}

impl FakeAudio {
    fn new(sender: oneshot::Sender<()>, renderer_expectations: RendererExpectations) -> Self {
        Self { sender, renderer_expectations }
    }

    async fn serve(self, mut request_stream: AudioRequestStream) -> Result<()> {
        let mut sender_option = Some(self.sender);

        while let Some(request) = request_stream.next().await {
            match request? {
                AudioRequest::CreateAudioRenderer { audio_renderer_request, .. } => {
                    spawn_log_error(
                        FakeAudioRenderer::new(
                            sender_option.take().expect("AudioRenderer created twice."),
                            self.renderer_expectations,
                        )
                        .serve(audio_renderer_request.into_stream()?),
                    );
                }
                _ => {
                    return Err(failure::format_err!("Unexpected Audio request received"));
                }
            }
        }

        Ok(())
    }
}

struct FakeAudioRenderer {
    renderer_expectations: RendererExpectations,
    sender: Option<oneshot::Sender<()>>,
}

impl FakeAudioRenderer {
    fn new(sender: oneshot::Sender<()>, renderer_expectations: RendererExpectations) -> Self {
        Self { sender: Some(sender), renderer_expectations }
    }

    async fn serve(mut self, mut request_stream: AudioRendererRequestStream) -> Result<()> {
        let mut payload_id: Option<u32> = None;
        let mut send_packet_responder_option: Option<AudioRendererSendPacketResponder> = None;

        let mut add_payload_buffer_called = false;
        let mut remove_payload_buffer_called = false;
        let mut send_packet_called = false;
        let mut set_pcm_stream_type_called = false;
        let mut play_called = false;
        let mut pause_no_reply_called = false;
        let mut set_usage_called = false;

        while let Some(request) = request_stream.next().await {
            assert!(request.is_ok());
            match request? {
                AudioRendererRequest::AddPayloadBuffer { id, payload_buffer, .. } => {
                    assert!(!add_payload_buffer_called);
                    add_payload_buffer_called = true;

                    assert!(!payload_id.is_some());
                    payload_id.replace(id);
                    assert!(
                        payload_buffer.get_koid().expect("Getting vmo koid")
                            == self.renderer_expectations.vmo_koid
                    );
                }
                AudioRendererRequest::RemovePayloadBuffer { id, .. } => {
                    assert!(pause_no_reply_called);
                    assert!(!remove_payload_buffer_called);
                    remove_payload_buffer_called = true;

                    assert!(id == payload_id.expect("RemovePayloadBuffer called with no buffer"));
                    let _ = payload_id.take();
                    self.sender.take().unwrap().send(()).expect("Sending on completion channel");
                }
                AudioRendererRequest::SendPacket { packet, responder, .. } => {
                    assert!(set_usage_called);
                    assert!(add_payload_buffer_called);
                    assert!(set_pcm_stream_type_called);
                    assert!(!send_packet_called);
                    send_packet_called = true;

                    assert!(packet == self.renderer_expectations.packet);
                    assert!(send_packet_responder_option.is_none());
                    send_packet_responder_option.replace(responder);
                }
                AudioRendererRequest::SetPcmStreamType { type_, .. } => {
                    assert!(!set_pcm_stream_type_called);
                    set_pcm_stream_type_called = true;

                    assert!(type_ == self.renderer_expectations.stream_type);
                }
                AudioRendererRequest::Play { reference_time: _, media_time, responder, .. } => {
                    assert!(send_packet_called);
                    assert!(!play_called);
                    play_called = true;

                    assert!(media_time == 0);
                    responder.send(0, 0).expect("Sending Play response");

                    assert!(send_packet_responder_option.is_some());
                    send_packet_responder_option
                        .take()
                        .expect("Play called before SendPacket")
                        .send()
                        .expect("Sending SendPacket response");
                }
                AudioRendererRequest::PauseNoReply { .. } => {
                    assert!(play_called);
                    assert!(!pause_no_reply_called);
                    pause_no_reply_called = true;
                }
                AudioRendererRequest::SetUsage { usage, .. } => {
                    assert!(!set_usage_called);
                    assert!(!set_pcm_stream_type_called);
                    set_usage_called = true;

                    assert!(usage == self.renderer_expectations.usage);
                }
                _ => {
                    return Err(failure::format_err!("Unexpected AudioRenderer request received"));
                }
            }
        }

        Ok(())
    }
}
