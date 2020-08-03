// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::spawn_log_error;
use crate::Result;
use anyhow::Context as _;
use fidl_fuchsia_media::*;
use fidl_fuchsia_media_sounds::*;
use fuchsia_async as fasync;
use fuchsia_component::client::{launch, App};
use fuchsia_component::server::*;
use fuchsia_zircon::{self as zx, prelude::AsHandleRef};
use futures::{channel::oneshot, future, join, select, StreamExt};
use matches::assert_matches;
use std::{fs::File, vec::Vec};

const SOUNDPLAYER_URL: &str = "fuchsia-pkg://fuchsia.com/soundplayer#meta/soundplayer.cmx";
const PAYLOAD_SIZE: usize = 1024;
const FRAME_SIZE: u32 = 2;
const USAGE: AudioRenderUsage = AudioRenderUsage::Media;

#[fasync::run_singlethreaded]
#[test]
async fn integration_buffer() -> Result<()> {
    let (sender, receiver) = oneshot::channel::<()>();
    let (mut buffer, koid, mut stream_type) = test_sound(PAYLOAD_SIZE);

    let service = TestService::new(
        sender,
        vec![RendererExpectations {
            vmo_koid: Some(koid),
            packets: vec![StreamPacket {
                pts: NO_TIMESTAMP,
                payload_buffer_id: 0,
                payload_offset: 0,
                payload_size: PAYLOAD_SIZE as u64,
                flags: 0,
                buffer_config: 0,
                stream_segment_id: 0,
            }],
            stream_type: stream_type.clone(),
            usage: USAGE,
            block_play: false,
        }],
    );

    service
        .sound_player
        .add_sound_buffer(0, &mut buffer, &mut stream_type)
        .expect("Calling add_sound_buffer");
    service
        .sound_player
        .play_sound(0, USAGE)
        .await
        .context("Calling play_sound")?
        .map_err(|err| anyhow::format_err!("Error playing sound: {:?}", err))?;
    service.sound_player.remove_sound(0).expect("Calling remove_sound");

    receiver.await.map_err(|_| anyhow::format_err!("Error awaiting test completion"))
}

#[fasync::run_singlethreaded]
#[test]
async fn integration_max_single_packet_buffer() -> Result<()> {
    let (sender, receiver) = oneshot::channel::<()>();
    let (mut buffer, koid, mut stream_type) =
        test_sound(MAX_FRAMES_PER_RENDERER_PACKET as usize * FRAME_SIZE as usize);

    let service = TestService::new(
        sender,
        vec![RendererExpectations {
            vmo_koid: Some(koid),
            packets: vec![StreamPacket {
                pts: NO_TIMESTAMP,
                payload_buffer_id: 0,
                payload_offset: 0,
                payload_size: MAX_FRAMES_PER_RENDERER_PACKET as u64 * FRAME_SIZE as u64,
                flags: 0,
                buffer_config: 0,
                stream_segment_id: 0,
            }],
            stream_type: stream_type.clone(),
            usage: USAGE,
            block_play: false,
        }],
    );

    service
        .sound_player
        .add_sound_buffer(0, &mut buffer, &mut stream_type)
        .expect("Calling add_sound_buffer");
    service
        .sound_player
        .play_sound(0, USAGE)
        .await
        .context("Calling play_sound")?
        .map_err(|err| anyhow::format_err!("Error playing sound: {:?}", err))?;
    service.sound_player.remove_sound(0).expect("Calling remove_sound");

    receiver.await.map_err(|_| anyhow::format_err!("Error awaiting test completion"))
}

#[fasync::run_singlethreaded]
#[test]
async fn integration_large_buffer() -> Result<()> {
    let (sender, receiver) = oneshot::channel::<()>();
    let (mut buffer, koid, mut stream_type) =
        test_sound((MAX_FRAMES_PER_RENDERER_PACKET as usize + 1) * FRAME_SIZE as usize);

    let service = TestService::new(
        sender,
        vec![RendererExpectations {
            vmo_koid: Some(koid),
            packets: vec![
                StreamPacket {
                    pts: NO_TIMESTAMP,
                    payload_buffer_id: 0,
                    payload_offset: 0,
                    payload_size: MAX_FRAMES_PER_RENDERER_PACKET as u64 * FRAME_SIZE as u64,
                    flags: 0,
                    buffer_config: 0,
                    stream_segment_id: 0,
                },
                StreamPacket {
                    pts: NO_TIMESTAMP,
                    payload_buffer_id: 0,
                    payload_offset: MAX_FRAMES_PER_RENDERER_PACKET as u64 * FRAME_SIZE as u64,
                    payload_size: FRAME_SIZE as u64,
                    flags: 0,
                    buffer_config: 0,
                    stream_segment_id: 0,
                },
            ],
            stream_type: stream_type.clone(),
            usage: USAGE,
            block_play: false,
        }],
    );

    service
        .sound_player
        .add_sound_buffer(0, &mut buffer, &mut stream_type)
        .expect("Calling add_sound_buffer");
    service
        .sound_player
        .play_sound(0, USAGE)
        .await
        .context("Calling play_sound")?
        .map_err(|err| anyhow::format_err!("Error playing sound: {:?}", err))?;
    service.sound_player.remove_sound(0).expect("Calling remove_sound");

    receiver.await.map_err(|_| anyhow::format_err!("Error awaiting test completion"))
}

const DURATION: i64 = 288412698;
const FILE_PAYLOAD_SIZE: u64 = 25438;

#[fasync::run_singlethreaded]
#[test]
async fn integration_file() -> Result<()> {
    let (sender, receiver) = oneshot::channel::<()>();

    let service = TestService::new(
        sender,
        vec![RendererExpectations {
            vmo_koid: None,
            packets: vec![StreamPacket {
                pts: NO_TIMESTAMP,
                payload_buffer_id: 0,
                payload_offset: 0,
                payload_size: FILE_PAYLOAD_SIZE,
                flags: 0,
                buffer_config: 0,
                stream_segment_id: 0,
            }],
            stream_type: AudioStreamType {
                sample_format: AudioSampleFormat::Signed16,
                channels: 1,
                frames_per_second: 44100,
            },
            usage: USAGE,
            block_play: false,
        }],
    );

    let duration = service
        .sound_player
        .add_sound_from_file(0, resource_file("sfx.wav").expect("Reading sound file"))
        .await
        .context("Calling add_sound_from_file")?;
    assert!(duration == Ok(DURATION));
    service
        .sound_player
        .play_sound(0, USAGE)
        .await
        .context("Calling play_sound")?
        .map_err(|err| anyhow::format_err!("Error playing sound: {:?}", err))?;
    service.sound_player.remove_sound(0).expect("Calling remove_sound");

    receiver.await.map_err(|_| anyhow::format_err!("Error awaiting test completion"))
}

#[fasync::run_singlethreaded]
#[test]
async fn integration_file_twice() -> Result<()> {
    let (sender, receiver) = oneshot::channel::<()>();

    let service = TestService::new(
        sender,
        vec![
            RendererExpectations {
                vmo_koid: None,
                packets: vec![StreamPacket {
                    pts: NO_TIMESTAMP,
                    payload_buffer_id: 0,
                    payload_offset: 0,
                    payload_size: FILE_PAYLOAD_SIZE,
                    flags: 0,
                    buffer_config: 0,
                    stream_segment_id: 0,
                }],
                stream_type: AudioStreamType {
                    sample_format: AudioSampleFormat::Signed16,
                    channels: 1,
                    frames_per_second: 44100,
                },
                usage: USAGE,
                block_play: false,
            },
            RendererExpectations {
                vmo_koid: None,
                packets: vec![StreamPacket {
                    pts: NO_TIMESTAMP,
                    payload_buffer_id: 0,
                    payload_offset: 0,
                    payload_size: FILE_PAYLOAD_SIZE,
                    flags: 0,
                    buffer_config: 0,
                    stream_segment_id: 0,
                }],
                stream_type: AudioStreamType {
                    sample_format: AudioSampleFormat::Signed16,
                    channels: 1,
                    frames_per_second: 44100,
                },
                usage: USAGE,
                block_play: false,
            },
        ],
    );

    let duration = service
        .sound_player
        .add_sound_from_file(0, resource_file("sfx.wav").expect("Reading sound file"))
        .await
        .context("Calling add_sound_from_file")?;
    assert!(duration == Ok(DURATION));
    service
        .sound_player
        .play_sound(0, USAGE)
        .await
        .context("Calling play_sound")?
        .map_err(|err| anyhow::format_err!("Error playing sound: {:?}", err))?;
    service
        .sound_player
        .play_sound(0, USAGE)
        .await
        .context("Calling play_sound")?
        .map_err(|err| anyhow::format_err!("Error playing sound: {:?}", err))?;
    service.sound_player.remove_sound(0).expect("Calling remove_sound");

    receiver.await.map_err(|_| anyhow::format_err!("Error awaiting test completion"))
}

#[fasync::run_singlethreaded]
#[test]
async fn integration_file_stop() -> Result<()> {
    let (sender, receiver) = oneshot::channel::<()>();

    let service = TestService::new(
        sender,
        vec![RendererExpectations {
            vmo_koid: None,
            packets: vec![StreamPacket {
                pts: NO_TIMESTAMP,
                payload_buffer_id: 0,
                payload_offset: 0,
                payload_size: FILE_PAYLOAD_SIZE,
                flags: 0,
                buffer_config: 0,
                stream_segment_id: 0,
            }],
            stream_type: AudioStreamType {
                sample_format: AudioSampleFormat::Signed16,
                channels: 1,
                frames_per_second: 44100,
            },
            usage: USAGE,
            block_play: true,
        }],
    );

    let duration = service
        .sound_player
        .add_sound_from_file(0, resource_file("sfx.wav").expect("Reading sound file"))
        .await
        .context("Calling add_sound_from_file")?;
    assert!(duration == Ok(DURATION));
    let play_sound = service.sound_player.play_sound(0, USAGE);
    let stop_sound = future::ready(service.sound_player.stop_playing_sound(0));

    // Call play_sound followed immediately by stop_playing_sound. The former will return a Stopped
    // error. The latter will succeed.
    let results = join! { play_sound, stop_sound };
    assert_matches!(results.0, Ok(Err(PlaySoundError::Stopped)));
    assert_matches!(results.1, Ok(()));

    service.sound_player.remove_sound(0).expect("Calling remove_sound");

    receiver.await.map_err(|_| anyhow::format_err!("Error awaiting test completion"))
}

#[fasync::run_singlethreaded]
#[test]
async fn integration_file_stop_second() -> Result<()> {
    let (sender, receiver) = oneshot::channel::<()>();

    let service = TestService::new(
        sender,
        vec![
            RendererExpectations {
                vmo_koid: None,
                packets: vec![StreamPacket {
                    pts: NO_TIMESTAMP,
                    payload_buffer_id: 0,
                    payload_offset: 0,
                    payload_size: FILE_PAYLOAD_SIZE,
                    flags: 0,
                    buffer_config: 0,
                    stream_segment_id: 0,
                }],
                stream_type: AudioStreamType {
                    sample_format: AudioSampleFormat::Signed16,
                    channels: 1,
                    frames_per_second: 44100,
                },
                usage: USAGE,
                block_play: true,
            },
            RendererExpectations {
                vmo_koid: None,
                packets: vec![StreamPacket {
                    pts: NO_TIMESTAMP,
                    payload_buffer_id: 0,
                    payload_offset: 0,
                    payload_size: FILE_PAYLOAD_SIZE,
                    flags: 0,
                    buffer_config: 0,
                    stream_segment_id: 0,
                }],
                stream_type: AudioStreamType {
                    sample_format: AudioSampleFormat::Signed16,
                    channels: 1,
                    frames_per_second: 44100,
                },
                usage: USAGE,
                block_play: true,
            },
        ],
    );

    let duration = service
        .sound_player
        .add_sound_from_file(0, resource_file("sfx.wav").expect("Reading sound file"))
        .await
        .context("Calling add_sound_from_file")?;
    assert!(duration == Ok(DURATION));
    let mut first_play_sound = service.sound_player.play_sound(0, USAGE);
    let second_play_sound = service.sound_player.play_sound(0, USAGE);
    let stop_sound = future::ready(service.sound_player.stop_playing_sound(0));

    // Call play_sound twice followed immediately by stop_playing_sound. The second play_sound will
    // return a Stopped error. stop_playing_sound will succeed. The first play_sound should not
    // terminate.
    select! {
        _ = first_play_sound => {
            assert!(false);
        }
        results = future::join(second_play_sound, stop_sound) => {
            assert_matches!(results.0, Ok(Err(PlaySoundError::Stopped)));
            assert_matches!(results.1, Ok(()));
        }
    }

    service.sound_player.remove_sound(0).expect("Calling remove_sound");

    receiver.await.map_err(|_| anyhow::format_err!("Error awaiting test completion"))
}

#[fasync::run_singlethreaded]
#[test]
async fn integration_file_bogus_stops() -> Result<()> {
    let (sender, receiver) = oneshot::channel::<()>();

    let service = TestService::new(
        sender,
        vec![RendererExpectations {
            vmo_koid: None,
            packets: vec![StreamPacket {
                pts: NO_TIMESTAMP,
                payload_buffer_id: 0,
                payload_offset: 0,
                payload_size: FILE_PAYLOAD_SIZE,
                flags: 0,
                buffer_config: 0,
                stream_segment_id: 0,
            }],
            stream_type: AudioStreamType {
                sample_format: AudioSampleFormat::Signed16,
                channels: 1,
                frames_per_second: 44100,
            },
            usage: USAGE,
            block_play: false,
        }],
    );

    let duration = service
        .sound_player
        .add_sound_from_file(0, resource_file("sfx.wav").expect("Reading sound file"))
        .await
        .context("Calling add_sound_from_file")?;
    assert!(duration == Ok(DURATION));

    // Stop a sound that hasn't been played.
    service.sound_player.stop_playing_sound(0)?;

    // Play the sound.
    service
        .sound_player
        .play_sound(0, USAGE)
        .await
        .context("Calling play_sound")?
        .map_err(|err| anyhow::format_err!("Error playing sound: {:?}", err))?;

    // Stop a sound that done playing.
    service.sound_player.stop_playing_sound(0)?;

    // Stop a sound that doesn't exist.
    service.sound_player.stop_playing_sound(1)?;

    service.sound_player.remove_sound(0).expect("Calling remove_sound");

    // Stop a sound that no longer exists.
    service.sound_player.stop_playing_sound(0)?;

    receiver.await.map_err(|_| anyhow::format_err!("Error awaiting test completion"))
}

/// Creates a file channel from a resource file.
fn resource_file(name: &str) -> Result<fidl::endpoints::ClientEnd<fidl_fuchsia_io::FileMarker>> {
    // We try two paths here, because normal components see their package data resources in
    // /pkg/data and shell tools see them in /pkgfs/packages/<pkg>>/0/data.
    Ok(fidl::endpoints::ClientEnd::<fidl_fuchsia_io::FileMarker>::new(zx::Channel::from(
        fdio::transfer_fd(
            File::open(format!("/pkg/data/{}", name)).context("Opening package data file")?,
        )?,
    )))
}

struct TestService {
    #[allow(unused)]
    app: App, // This needs to stay alive to keep the service running.
    #[allow(unused)]
    environment: NestedEnvironment, // This needs to stay alive to keep the service running.
    sound_player: PlayerProxy,
}

impl TestService {
    fn new(
        sender: oneshot::Sender<()>,
        mut renderer_expectations: Vec<RendererExpectations>,
    ) -> Self {
        let mut service_fs = ServiceFs::new();
        let mut sender_option = Some(sender);

        service_fs
            .add_fidl_service(move |request_stream: AudioRequestStream| {
                let r = renderer_expectations.pop().expect("Audio service created too many times.");
                spawn_log_error(
                    FakeAudio::new(
                        r,
                        if renderer_expectations.is_empty() { sender_option.take() } else { None },
                    )
                    .serve(request_stream),
                );
            })
            .add_proxy_service::<fidl_fuchsia_logger::LogSinkMarker, ()>();

        let environment = service_fs
            .create_salted_nested_environment("soundplayer-integration-test")
            .expect("Creating nested environment");
        let app = launch(environment.launcher(), String::from(SOUNDPLAYER_URL), None)
            .expect("Launching soundplayer");
        let sound_player = app.connect_to_service::<PlayerMarker>().expect("Connecting to Player");

        fasync::Task::local(service_fs.collect::<()>()).detach();

        Self { app, environment, sound_player }
    }
}

fn test_sound(size: usize) -> (fidl_fuchsia_mem::Buffer, zx::Koid, AudioStreamType) {
    let vmo = zx::Vmo::create(size as u64).expect("Creating VMO");
    let koid = vmo.get_koid().expect("Getting vmo koid");
    assert!(FRAME_SIZE % std::mem::size_of::<i16>() as u32 == 0);
    let stream_type = AudioStreamType {
        sample_format: AudioSampleFormat::Signed16,
        channels: FRAME_SIZE / std::mem::size_of::<i16>() as u32,
        frames_per_second: 44100,
    };

    (fidl_fuchsia_mem::Buffer { vmo: vmo, size: size as u64 }, koid, stream_type)
}

struct RendererExpectations {
    vmo_koid: Option<zx::Koid>,
    packets: Vec<StreamPacket>,
    stream_type: AudioStreamType,
    usage: AudioRenderUsage,
    block_play: bool,
}

struct FakeAudio {
    renderer_expectations: Option<RendererExpectations>,
    sender: Option<oneshot::Sender<()>>,
}

impl FakeAudio {
    fn new(
        renderer_expectations: RendererExpectations,
        sender: Option<oneshot::Sender<()>>,
    ) -> Self {
        Self { renderer_expectations: Some(renderer_expectations), sender }
    }

    async fn serve(mut self, mut request_stream: AudioRequestStream) -> Result<()> {
        while let Some(request) = request_stream.next().await {
            match request? {
                AudioRequest::CreateAudioRenderer { audio_renderer_request, .. } => {
                    spawn_log_error(
                        FakeAudioRenderer::new(
                            self.renderer_expectations.take().unwrap(),
                            self.sender.take(),
                        )
                        .serve(audio_renderer_request.into_stream()?),
                    );
                }
                _ => {
                    return Err(anyhow::format_err!("Unexpected Audio request received"));
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
    fn new(
        renderer_expectations: RendererExpectations,
        sender: Option<oneshot::Sender<()>>,
    ) -> Self {
        Self { renderer_expectations, sender }
    }

    async fn serve(mut self, mut request_stream: AudioRendererRequestStream) -> Result<()> {
        let mut payload_id: Option<u32> = None;
        let mut send_packet_responder_option: Option<AudioRendererSendPacketResponder> = None;
        let mut play_responder_option: Option<AudioRendererPlayResponder> = None;

        let mut add_payload_buffer_called = false;
        let mut send_packet_called = false;
        let mut set_pcm_stream_type_called = false;
        let mut play_called = false;
        let mut set_usage_called = false;
        let mut packet_index = 0;

        while let Some(request) = request_stream.next().await {
            assert!(request.is_ok());
            match request? {
                AudioRendererRequest::AddPayloadBuffer { id, payload_buffer, .. } => {
                    assert!(!add_payload_buffer_called);
                    add_payload_buffer_called = true;

                    assert!(!payload_id.is_some());
                    payload_id.replace(id);
                    if let Some(vmo_koid) = self.renderer_expectations.vmo_koid {
                        assert!(payload_buffer.get_koid().expect("Getting vmo koid") == vmo_koid);
                    }
                }
                AudioRendererRequest::SendPacket { packet, responder, .. } => {
                    assert!(set_usage_called);
                    assert!(add_payload_buffer_called);
                    assert!(set_pcm_stream_type_called);
                    assert!(!send_packet_called);
                    send_packet_called = true;

                    // Only the last packet is sent with this method.
                    assert!(packet_index == self.renderer_expectations.packets.len() - 1);
                    assert!(packet == self.renderer_expectations.packets[packet_index]);
                    assert!(send_packet_responder_option.is_none());
                    send_packet_responder_option.replace(responder);
                    packet_index += 1;
                }
                AudioRendererRequest::SendPacketNoReply { packet, .. } => {
                    assert!(set_usage_called);
                    assert!(add_payload_buffer_called);
                    assert!(set_pcm_stream_type_called);

                    // The last packet is not sent with this method.
                    assert!(packet_index < self.renderer_expectations.packets.len() - 1);
                    assert!(packet == self.renderer_expectations.packets[packet_index]);
                    assert!(send_packet_responder_option.is_none());
                    packet_index += 1;
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
                    assert!(send_packet_responder_option.is_some());

                    if self.renderer_expectations.block_play {
                        // Keep this method pending for the rest of the renderer's lifetime.
                        play_responder_option.replace(responder);
                    } else {
                        responder.send(0, 0).expect("Sending Play response");
                        send_packet_responder_option
                            .take()
                            .expect("Play called before SendPacket")
                            .send()
                            .expect("Sending SendPacket response");
                    }

                    if let Some(sender) = self.sender.take() {
                        sender.send(()).expect("Sending on completion channel");
                    }
                }
                AudioRendererRequest::SetUsage { usage, .. } => {
                    assert!(!set_usage_called);
                    assert!(!set_pcm_stream_type_called);
                    set_usage_called = true;

                    assert!(usage == self.renderer_expectations.usage);
                }
                _ => {
                    return Err(anyhow::format_err!("Unexpected AudioRenderer request received"));
                }
            }
        }

        Ok(())
    }
}
