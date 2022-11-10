// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_RING_BUFFER_CONSUMER_WRITER_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_RING_BUFFER_CONSUMER_WRITER_H_

#include <fidl/fuchsia.media2/cpp/wire.h>
#include <lib/fidl/cpp/wire/client.h>

#include <memory>
#include <optional>

#include "src/media/audio/lib/format2/stream_converter.h"
#include "src/media/audio/services/mixer/common/memory_mapped_buffer.h"
#include "src/media/audio/services/mixer/mix/consumer_stage.h"
#include "src/media/audio/services/mixer/mix/ring_buffer.h"

namespace media_audio {

// Enables consumers to write to a ring buffer.
class RingBufferConsumerWriter : public ConsumerStage::Writer {
 public:
  // Writes data of `source_format` to the ring buffer. The `source_format` must not differ from
  // `buffer->format()` except in sample type.
  RingBufferConsumerWriter(std::shared_ptr<RingBuffer> buffer, const Format& source_format);

  // Implements ConsumerStage::Writer.
  void WriteData(int64_t start_frame, int64_t frame_count, const void* payload) final;
  void WriteSilence(int64_t start_frame, int64_t frame_count) final;
  void End() final;

 private:
  void WriteInternal(int64_t start_frame, int64_t frame_count, const void* payload);

  const std::shared_ptr<StreamConverter> stream_converter_;
  const std::shared_ptr<RingBuffer> buffer_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_RING_BUFFER_CONSUMER_WRITER_H_
