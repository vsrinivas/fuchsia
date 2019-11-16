// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_STREAM_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_STREAM_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/fit/result.h>
#include <lib/zx/time.h>

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "src/media/audio/audio_core/format.h"
#include "src/media/audio/audio_core/packet.h"

namespace media::audio {

class Stream : public fbl::RefCounted<Stream> {
 public:
  Stream(Format format) : format_(format) {}
  virtual ~Stream() = default;

  // When consuming audio, destinations must always pair their calls to LockPacket and UnlockPacket,
  // even if the front of the queue was nullptr.
  //
  // Doing so ensures that sources which are attempting to flush the pending queue are forced to
  // wait if the front of the queue is involved in a mixing operation. This, in turn, guarantees
  // that audio packets are always returned to the user in the order which they were queued in
  // without forcing AudioRenderers to wait to queue new data if a mix operation is in progress.
  virtual fbl::RefPtr<Packet> LockPacket(bool* was_flushed) = 0;
  virtual void UnlockPacket(bool release_packet) = 0;

  const Format& format() const { return format_; }

 private:
  Format format_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_STREAM_H_
