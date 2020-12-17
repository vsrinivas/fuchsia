// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    bt_a2dp::{codec::MediaCodecConfig, media_task::*},
    bt_avdtp::MediaStream,
    fidl_fuchsia_media::{AudioChannelId, AudioPcmMode, PcmFormat},
    fuchsia_async as fasync,
    fuchsia_bluetooth::{inspect::DataStreamInspect, types::PeerId},
    fuchsia_trace as trace,
    futures::{
        channel::oneshot,
        future::{BoxFuture, Shared},
        lock::Mutex,
        AsyncWriteExt, FutureExt, TryFutureExt, TryStreamExt,
    },
    log::{info, trace, warn},
    std::sync::Arc,
};

use crate::encoding::EncodedStream;
use crate::sources;

/// SourceTaskBuilder is a MediaTaskBuilder will build `ConfiguredSourceTask`s when configured.
/// `source_type` determines where the source of audio is provided.
/// When configured, a test stream is created to confirm that it is possible to stream audio using
/// the configuration.  This stream is discarded and the stream is restarted when the resulting
/// `ConfiguredSourceTask` is started.
#[derive(Clone)]
pub struct SourceTaskBuilder {
    /// The type of source audio.
    source_type: sources::AudioSourceType,
}

impl MediaTaskBuilder for SourceTaskBuilder {
    fn configure(
        &self,
        peer_id: &PeerId,
        codec_config: &MediaCodecConfig,
        data_stream_inspect: DataStreamInspect,
    ) -> BoxFuture<'static, Result<Box<dyn MediaTaskRunner>, MediaTaskError>> {
        let res = self.configure_task(peer_id, codec_config, data_stream_inspect);
        Box::pin(async { Ok::<Box<dyn MediaTaskRunner>, _>(Box::new(res?)) })
    }
}

impl SourceTaskBuilder {
    /// Make a new builder that will source audio from `source_type`.  See `sources::build_stream`
    /// for documentation on the types of streams that are available.
    pub fn new(source_type: sources::AudioSourceType) -> Self {
        Self { source_type }
    }

    pub(crate) fn configure_task(
        &self,
        peer_id: &PeerId,
        codec_config: &MediaCodecConfig,
        data_stream_inspect: DataStreamInspect,
    ) -> Result<ConfiguredSourceTask, MediaTaskError> {
        let channel_map = match codec_config.channel_count() {
            Ok(1) => vec![AudioChannelId::Cf],
            Ok(2) => vec![AudioChannelId::Lf, AudioChannelId::Rf],
            Ok(_) | Err(_) => return Err(MediaTaskError::ConfigurationNotSupported),
        };
        let pcm_format = PcmFormat {
            pcm_mode: AudioPcmMode::Linear,
            bits_per_sample: 16,
            frames_per_second: codec_config.sampling_frequency()?,
            channel_map,
        };
        let source_stream = sources::build_stream(&peer_id, pcm_format.clone(), self.source_type)
            .map_err(|_e| MediaTaskError::ConfigurationNotSupported)?;
        if let Err(e) = EncodedStream::build(pcm_format.clone(), source_stream, codec_config) {
            trace!("SourceTaskBuilder: can't build encoded stream: {:?}", e);
            return Err(MediaTaskError::Other(format!("Can't build encoded stream: {}", e)));
        }
        Ok(ConfiguredSourceTask::build(
            pcm_format,
            self.source_type,
            peer_id.clone(),
            codec_config,
            data_stream_inspect,
        ))
    }
}

/// Provides audio from this to the MediaStream when started.  Streams are created and started when
/// this task is started, and destoyed when stopped.
pub(crate) struct ConfiguredSourceTask {
    /// The type of source audio.
    source_type: sources::AudioSourceType,
    /// Format the source audio should be produced in.
    pub(crate) pcm_format: PcmFormat,
    /// Id of the peer that will be receiving the stream.  Used to distinguish sources for Fuchsia
    /// Media.
    peer_id: PeerId,
    /// Configuration providing the format of encoded audio requested by the peer.
    codec_config: MediaCodecConfig,
    /// Data Stream inspect object for tracking total bytes / current transfer speed.
    data_stream_inspect: Arc<Mutex<DataStreamInspect>>,
}

impl ConfiguredSourceTask {
    /// Build a new ConfiguredSourceTask.  Usually only called by SourceTaskBuilder.
    /// `ConfiguredSourceTask::start` will only return errors if the settings here cannot produce a
    /// stream.  No checks are done when building.
    pub(crate) fn build(
        pcm_format: PcmFormat,
        source_type: sources::AudioSourceType,
        peer_id: PeerId,
        codec_config: &MediaCodecConfig,
        data_stream_inspect: DataStreamInspect,
    ) -> Self {
        Self {
            pcm_format,
            source_type,
            peer_id,
            codec_config: codec_config.clone(),
            data_stream_inspect: Arc::new(Mutex::new(data_stream_inspect)),
        }
    }
}

impl MediaTaskRunner for ConfiguredSourceTask {
    fn start(&mut self, stream: MediaStream) -> Result<Box<dyn MediaTask>, MediaTaskError> {
        let source_stream =
            sources::build_stream(&self.peer_id, self.pcm_format.clone(), self.source_type)
                .map_err(|e| MediaTaskError::Other(format!("Building stream: {}", e)))?;
        let encoded_stream =
            EncodedStream::build(self.pcm_format.clone(), source_stream, &self.codec_config)
                .map_err(|e| MediaTaskError::Other(format!("Can't build encoded stream: {}", e)))?;
        let stream_task = RunningSourceTask::build(
            self.codec_config.clone(),
            encoded_stream,
            stream,
            self.data_stream_inspect.clone(),
        );
        let _ = self.data_stream_inspect.try_lock().map(|mut l| l.start());
        Ok(Box::new(stream_task))
    }
}

struct RunningSourceTask {
    stream_task: Option<fasync::Task<()>>,
    result_fut: Shared<BoxFuture<'static, Result<(), MediaTaskError>>>,
}

impl RunningSourceTask {
    /// The main streaming task. Reads encoded audio from the encoded_stream and packages into RTP
    /// packets, sending the resulting RTP packets using `media_stream`.
    async fn stream_task(
        codec_config: MediaCodecConfig,
        mut encoded_stream: EncodedStream,
        mut media_stream: MediaStream,
        data_stream_inspect: Arc<Mutex<DataStreamInspect>>,
    ) -> Result<(), Error> {
        let frames_per_encoded = codec_config.pcm_frames_per_encoded_frame() as u32;
        let max_tx_size = media_stream.max_tx_size()?;
        let mut packet_builder = codec_config.make_packet_builder(max_tx_size)?;
        loop {
            let encoded = match encoded_stream.try_next().await? {
                None => continue,
                Some(encoded) => encoded,
            };
            let packets = match packet_builder.add_frame(encoded, frames_per_encoded) {
                Err(e) => {
                    warn!("Can't add packet to RTP packet: {:?}", e);
                    continue;
                }
                Ok(packets) => packets,
            };

            for packet in packets {
                trace::duration_begin!("bt-a2dp-source", "Media:PacketSent");
                if let Err(e) = media_stream.write(&packet).await {
                    info!("Failed sending packet to peer: {}", e);
                    trace::duration_end!("bt-a2dp-source", "Media:PacketSent");
                    return Ok(());
                }
                let _ = data_stream_inspect.try_lock().map(|mut l| {
                    l.record_transferred(packet.len(), fasync::Time::now());
                });
                trace::duration_end!("bt-a2dp-source", "Media:PacketSent");
            }
        }
    }

    fn build(
        codec_config: MediaCodecConfig,
        encoded_stream: EncodedStream,
        media_stream: MediaStream,
        inspect: Arc<Mutex<DataStreamInspect>>,
    ) -> Self {
        let (sender, receiver) = oneshot::channel();
        let stream_task_fut =
            Self::stream_task(codec_config, encoded_stream, media_stream, inspect);
        let wrapped_task = fasync::Task::spawn(async move {
            trace::instant!("bt-a2dp-source", "Media:Start", trace::Scope::Thread);
            let result = stream_task_fut
                .await
                .map_err(|e| MediaTaskError::Other(format!("Error in streaming audio: {}", e)));
            let _ = sender.send(result);
        });
        let result_fut = receiver.map_ok_or_else(|_err| Ok(()), |result| result).boxed().shared();
        Self { stream_task: Some(wrapped_task), result_fut }
    }
}

impl MediaTask for RunningSourceTask {
    fn finished(&mut self) -> BoxFuture<'static, Result<(), MediaTaskError>> {
        self.result_fut.clone().boxed()
    }

    fn stop(&mut self) -> Result<(), MediaTaskError> {
        if let Some(_task) = self.stream_task.take() {
            trace::instant!("bt-a2dp-source", "Media:Stopped", trace::Scope::Thread);
        }
        // Either a result already happened, or we just sent an Ok(()) by dropping the result
        // sender.
        self.result().unwrap_or(Ok(()))
    }
}

#[cfg(all(test, feature = "test_encoding"))]
mod tests {
    use super::*;

    use bt_a2dp::{codec::MediaCodecConfig, media_types::*};
    use bt_avdtp::MediaCodecType;
    use fuchsia_bluetooth::types::Channel;
    use fuchsia_inspect as inspect;
    use fuchsia_inspect_derive::WithInspect;
    use futures::StreamExt;
    use parking_lot::Mutex;
    use std::sync::RwLock;
    use test_util::assert_gt;

    #[test]
    fn configures_source_from_codec_config() {
        let _exec = fasync::Executor::new().expect("failed to create an executor");
        let builder = SourceTaskBuilder::new(sources::AudioSourceType::BigBen);

        // Minimum SBC requirements are mono, 48kHz
        let mono_config = MediaCodecConfig::min_sbc();
        let task = builder
            .configure_task(&PeerId(1), &mono_config, DataStreamInspect::default())
            .expect("should build okay");
        assert_eq!(48000, task.pcm_format.frames_per_second);
        assert_eq!(1, task.pcm_format.channel_map.len());

        // A standard SBC audio config which is stereo and 44.1kHz
        let sbc_codec_info = SbcCodecInfo::new(
            SbcSamplingFrequency::FREQ44100HZ,
            SbcChannelMode::JOINT_STEREO,
            SbcBlockCount::SIXTEEN,
            SbcSubBands::EIGHT,
            SbcAllocation::LOUDNESS,
            SbcCodecInfo::BITPOOL_MIN,
            SbcCodecInfo::BITPOOL_MAX,
        )
        .unwrap();
        let stereo_config =
            MediaCodecConfig::build(MediaCodecType::AUDIO_SBC, &sbc_codec_info.to_bytes().to_vec())
                .unwrap();

        let task = builder
            .configure_task(&PeerId(1), &stereo_config, DataStreamInspect::default())
            .expect("should build okay");
        assert_eq!(44100, task.pcm_format.frames_per_second);
        assert_eq!(2, task.pcm_format.channel_map.len());
    }

    #[test]
    fn source_media_stream_stats() {
        let mut exec = fasync::Executor::new().expect("executor should build");
        let builder = SourceTaskBuilder::new(sources::AudioSourceType::BigBen);

        let inspector = inspect::component::inspector();
        let root = inspector.root();
        let d = DataStreamInspect::default().with_inspect(root, "stream").expect("attach to tree");

        // Minimum SBC requirements are mono, 48kHz
        let mono_config = MediaCodecConfig::min_sbc();
        let mut task =
            builder.configure_task(&PeerId(1), &mono_config, d).expect("should build okay");

        let (mut remote, local) = Channel::create();
        let local = Arc::new(RwLock::new(local));
        let weak_local = Arc::downgrade(&local);
        let stream = MediaStream::new(Arc::new(Mutex::new(true)), weak_local);

        let _running_task = task.start(stream).expect("media should start");

        let _ = exec.run_singlethreaded(remote.next()).expect("some packet");

        let hierarchy =
            exec.run_singlethreaded(inspect::reader::read(inspector)).expect("got hierarchy");

        // We don't know exactly how many were sent at this point, but make sure we got at
        // least some recorded.
        let total_bytes = hierarchy
            .get_property_by_path(&vec!["stream", "total_bytes"])
            .expect("missing property");
        assert_gt!(total_bytes.uint().expect("uint"), &0);

        let bytes_per_second_current = hierarchy
            .get_property_by_path(&vec!["stream", "bytes_per_second_current"])
            .expect("missing property");
        assert_gt!(bytes_per_second_current.uint().expect("uint"), &0);
    }
}
