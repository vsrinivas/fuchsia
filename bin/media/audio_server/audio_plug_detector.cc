// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/audio_plug_detector.h"

#include <dirent.h>
#include <fcntl.h>
#include <zircon/compiler.h>
#include <zircon/device/audio.h>
#include <zircon/device/device.h>
#include <zircon/device/vfs.h>
#include <zx/channel.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/macros.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "garnet/bin/media/audio_server/audio_output.h"
#include "garnet/bin/media/audio_server/audio_output_manager.h"
#include "garnet/bin/media/audio_server/platform/driver_output.h"
#include "lib/fxl/files/unique_fd.h"

namespace media {
namespace audio {

static const char* AUDIO_OUTPUT_DEVNODES = "/dev/class/audio-output";

AudioPlugDetector::~AudioPlugDetector() {
  FXL_DCHECK(manager_ == nullptr);
}

MediaResult AudioPlugDetector::Start(AudioOutputManager* manager) {
  FXL_DCHECK(manager != nullptr);

  // If we fail to set up monitoring for any of our target directories,
  // automatically stop monitoring all sources of device nodes.
  auto error_cleanup = fbl::MakeAutoCall([this]() { Stop(); });

  // If we are already running, we cannot start again.  Cancel the cleanup
  // operation and report that things are successfully started.
  if (manager_ != nullptr) {
    FXL_DLOG(WARNING) << "Attempted to start the AudioPlugDetector twice!";
    error_cleanup.cancel();
    return MediaResult::OK;
  }

  // Record our new manager
  manager_ = manager;

  watcher_ = fsl::DeviceWatcher::Create(AUDIO_OUTPUT_DEVNODES,
      [this](int dir_fd, std::string filename) {
        AddAudioDevice(dir_fd, filename);
      });

  if (watcher_ == nullptr) {
    FXL_LOG(ERROR)
        << "AudioPlugDetector failed to create DeviceWatcher for \""
        << AUDIO_OUTPUT_DEVNODES
        << "\".";
    return MediaResult::INSUFFICIENT_RESOURCES;
  }

  error_cleanup.cancel();

  return MediaResult::OK;
}

void AudioPlugDetector::Stop() {
  manager_ = nullptr;
  watcher_.reset();
}

void AudioPlugDetector::AddAudioDevice(int dir_fd, const std::string& name) {
  if (manager_ == nullptr)
    return;

  // Open the device node.
  fxl::UniqueFD dev_node(::openat(dir_fd, name.c_str(), O_RDONLY));
  if (!dev_node.is_valid()) {
    FXL_LOG(WARNING) << "AudioPlugDetector failed to open device node at \""
                     << name << "\". ("
                     << strerror(errno) << " : " << errno
                     << ")";
    return;
  }

  AudioOutputPtr new_output;
  zx::channel channel;
  ssize_t res;

  res = ioctl_audio_get_channel(dev_node.get(),
                                 channel.reset_and_get_address());
  if (res < 0) {
    FXL_LOG(INFO) << "Failed to open channel to Audio output (res " << res
                  << ")";
    return;
  }

  new_output = DriverOutput::Create(std::move(channel), manager_);
  if (new_output == nullptr) {
    FXL_LOG(WARNING) << "Failed to instantiate audio output for \"" << name
                     << "\"";
    return;
  }

  manager_->AddOutput(std::move(new_output));
}

}  // namespace audio
}  // namespace media
