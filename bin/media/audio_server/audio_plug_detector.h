// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/types.h>
#include <mxtl/mutex.h>
#include <mxtl/ref_ptr.h>

#include "apps/media/services/media_result.fidl.h"
#include "drivers/audio/dispatcher-pool/dispatcher-channel.h"
#include "lib/ftl/files/unique_fd.h"

namespace media {
namespace audio {

class AudioOutputManager;

class AudioPlugDetector : public ::audio::DispatcherChannel::Owner {
 public:
  // Create helper.  AudioPlugDetectors are intrusively ref-counted objects, so
  // we force instantiation via a static Create method (which produces a
  // mxtl::RefPtr<>) instead of exposing the constructor.
  static mxtl::RefPtr<AudioPlugDetector> Create() {
    return mxtl::AdoptRef(new AudioPlugDetector());
  }

  MediaResult Start(AudioOutputManager* manager) __TA_EXCLUDES(process_lock_);
  void Stop() __TA_EXCLUDES(process_lock_);

 protected:
  mx_status_t ProcessChannel(::audio::DispatcherChannel& channel,
                             const mx_io_packet_t& io_packet) final
      __TA_EXCLUDES(process_lock_);

 private:
  friend class mxtl::RefPtr<AudioPlugDetector>;

  enum class DevNodeType { AUDIO, AUDIO2_OUTPUT };

  struct WatchTarget {
    const char* node_dir;
    DevNodeType type;
  };

  AudioPlugDetector() {}
  ~AudioPlugDetector();

  void AddAudioDeviceLocked(const char* node_name,
                            const WatchTarget& watch_target)
      __TA_REQUIRES(process_lock_);

  // TODO(johngro) : Turn this into a reader/writer lock.  We should be allowed
  // to add multiple devices in parallel (if we need to), we just need to lock
  // for "write" when shutting down.
  mxtl::Mutex process_lock_;

  // TODO(johngro) : Deal with the thread safty issues here!  If we queue an
  // AddDevice callback on the output manager, there is nothing keeping the
  // manager alive while the callback is in flight.  If the manager is shutdown
  // and then this callback is executed, this will access free'd memory.
  AudioOutputManager* manager_ __TA_GUARDED(process_lock_) = nullptr;
};

}  // namespace audio
}  // namespace media
