// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/audio_server/audio_plug_detector.h"

#include <dirent.h>
#include <fcntl.h>
#include <magenta/compiler.h>
#include <magenta/device/audio.h>
#include <magenta/device/audio2.h>
#include <magenta/device/device.h>
#include <magenta/device/vfs.h>
#include <mx/channel.h>
#include <mxtl/auto_call.h>
#include <mxtl/auto_lock.h>
#include <mxtl/macros.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "apps/media/src/audio_server/audio_output.h"
#include "apps/media/src/audio_server/audio_output_manager.h"
#include "apps/media/src/audio_server/platform/magenta/magenta_output.h"
#include "apps/media/src/audio_server/platform/usb/usb_output.h"
#include "lib/ftl/files/unique_fd.h"

namespace media {
namespace audio {

static const char* AUDIO_DEVNODES = "/dev/class/audio";
static const char* AUDIO2_OUTPUT_DEVNODES = "/dev/class/audio2-output";

AudioPlugDetector::~AudioPlugDetector() {
  FTL_DCHECK(manager_ == nullptr);
}

MediaResult AudioPlugDetector::Start(AudioOutputManager* manager) {
  struct {
    const char* node_dir;
    DevNodeType type;
  } WATCH_TARGETS[] = {
      {.node_dir = AUDIO_DEVNODES, .type = DevNodeType::AUDIO},
      {.node_dir = AUDIO2_OUTPUT_DEVNODES, .type = DevNodeType::AUDIO2_OUTPUT},
  };

  FTL_DCHECK(manager != nullptr);

  // If we fail to set up monitoring for any of our target directories,
  // automatically stop monitoring all sources of device nodes.
  auto error_cleanup = mxtl::MakeAutoCall([this]() { Stop(); });

  // If we are already running, we cannot start again.  Cancel the cleanup
  // operation and report that things are successfully started.
  if (manager_ != nullptr) {
    FTL_DLOG(WARNING) << "Attempted to start the AudioPlugDetector twice!";
    error_cleanup.cancel();
    return MediaResult::OK;
  }

  // Record our new manager
  manager_ = manager;

  // Size our watcher vector.
  watchers_.reserve(countof(WATCH_TARGETS));

  for (const auto& target : WATCH_TARGETS) {
    auto watcher = mtl::DeviceWatcher::Create(target.node_dir,
        [this, type = target.type](int dir_fd, std::string filename) {
          AddAudioDevice(dir_fd, filename, type);
        });

    if (watcher == nullptr) {
      FTL_LOG(ERROR)
          << "AudioPlugDetector failed to create DeviceWatcher for \""
          << target.node_dir
          << "\".";
      return MediaResult::INSUFFICIENT_RESOURCES;
    }

    watchers_.push_back(mxtl::move(watcher));
  }

  error_cleanup.cancel();

  return MediaResult::OK;
}

void AudioPlugDetector::Stop() {
  manager_ = nullptr;
  watchers_.clear();
}

void AudioPlugDetector::AddAudioDevice(int dir_fd,
                                       const std::string& name,
                                       DevNodeType type) {
  if (manager_ == nullptr)
    return;

  // Open the device node.
  ftl::UniqueFD dev_node(
      ::openat(dir_fd,
               name.c_str(),
               type == DevNodeType::AUDIO ? O_RDWR : O_RDONLY));
  if (!dev_node.is_valid()) {
    FTL_LOG(WARNING) << "AudioPlugDetector failed to open device node at \""
                     << name << "\". ("
                     << strerror(errno) << " : " << errno
                     << ")";
    return;
  }

  AudioOutputPtr new_output;

  switch (type) {
    case DevNodeType::AUDIO2_OUTPUT: {
      mx::channel channel;
      ssize_t res;

      res = ioctl_audio2_get_channel(dev_node.get(),
                                     channel.reset_and_get_address());
      if (res < 0) {
        FTL_LOG(INFO) << "Failed to open channel to Audio2 output (res " << res
                      << ")";
        return;
      }

      new_output = MagentaOutput::Create(std::move(channel), manager_);
    } break;

    // TODO(johngro) Get rid of this once USB has been converted to the new
    // audio interface
    case DevNodeType::AUDIO: {
      int device_type;
      int res = ioctl_audio_get_device_type(dev_node.get(), &device_type);

      if (res != sizeof(device_type)) {
        FTL_DLOG(WARNING) << "Failed to get device type for \"" << name
                          << "\" (res " << res << ")";
        return;
      }

      if (device_type != AUDIO_TYPE_SINK) {
        FTL_DLOG(INFO) << "Unsupported USB audio device (type " << device_type
                       << ") at \"" << name << "\".  Skipping";
        return;
      }

      new_output = UsbOutput::Create(std::move(dev_node), manager_);
      break;
    }

    default: {
      FTL_LOG(WARNING) << "Unexpected DevNodeType ("
                       << static_cast<uint>(type)
                       << ") while attempting to add device.";
      return;
    }
  }

  if (new_output == nullptr) {
    FTL_LOG(WARNING) << "Failed to instantiate audio output for \"" << name
                     << "\"";
    return;
  }

  manager_->AddOutput(std::move(new_output));
}

}  // namespace audio
}  // namespace media
