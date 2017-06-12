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
  static const WatchTarget WATCH_TARGETS[] = {
      {.node_dir = AUDIO_DEVNODES, .type = DevNodeType::AUDIO},
      {.node_dir = AUDIO2_OUTPUT_DEVNODES, .type = DevNodeType::AUDIO2_OUTPUT},
  };

  FTL_DCHECK(manager != nullptr);

  // If we fail to set up monitoring for any of our target directories,
  // automatically stop monitoring of all sources of device nodes.
  auto error_cleanup = mxtl::MakeAutoCall([this]() { Stop(); });

  {
    mxtl::AutoLock process_lock(&process_lock_);

    // If we are already running, we cannot start again.  Cancel the cleanup
    // operation and report that things are successfully started.
    if (manager_ != nullptr) {
      FTL_DLOG(WARNING) << "Attempted to start the AudioPlugDetector twice!";
      error_cleanup.cancel();
      return MediaResult::OK;
    }

    // Record our new manager
    manager_ = manager;

    // For each watch target...
    // 1) Open the directory where dev-nodes get published.
    // 2) Create a DispatcherChannel channel using the dev node type as the
    //    owner ctx (so we know what type of device we need to attach to without
    //    needing to parse the path it was created in).
    // 3) Create a magenta channel which watches the directory for new device
    //    nodes being added.
    // 4) Activate the DispatcherChannel, binding it to ourselves, and the
    //    magenta channel we just created.
    // 5) Finally, manually enumerate the files which were there when we
    //    started.
    //
    // TODO(johngro) : This runs a (small) risk of duplicating detection of dev
    // nodes.  See MG-638 for details.  When watchers have the ability to
    // atomically enumerate a directory's initial contents, do that instead of
    // Step #5
    for (const auto& target : WATCH_TARGETS) {
      // Step #1
      ftl::UniqueFD dirfd(::open(target.node_dir, O_RDONLY | O_DIRECTORY));
      if (!dirfd.is_valid()) {
        FTL_LOG(ERROR) << "AudioPlugDetector failed to open \""
                       << target.node_dir
                       << "\" when attempting to start monitoring for new "
                          "device nodes.  (errno "
                       << errno << ")";
        return MediaResult::NOT_FOUND;
      }

      // Step #2
      mx::channel watcher;
      ssize_t res;
      res = ioctl_vfs_watch_dir(dirfd.get(), watcher.get_address());
      if (res < 0) {
        FTL_LOG(ERROR) << "AudioPlugDetector failed to create watcher for \""
                       << target.node_dir
                       << "\" when attempting to start monitoring for new "
                          "device nodes.  (res "
                       << res << ")";
        return MediaResult::INTERNAL_ERROR;
      }

      // Step #3
      auto dispatcher = ::audio::DispatcherChannelAllocator::New(
          reinterpret_cast<uintptr_t>(&target));
      if (dispatcher == nullptr) {
        FTL_LOG(ERROR)
            << "AudioPlugDetector failed to create DispatcherChannel for \""
            << target.node_dir
            << "\" when attempting to start monitoring for new device nodes.  "
               "(res "
            << res << ")";
        return MediaResult::INSUFFICIENT_RESOURCES;
      }

      // Step #4
      mx_status_t mx_res;
      mx_res = dispatcher->Activate(mxtl::WrapRefPtr(this), std::move(watcher));
      if (mx_res != MX_OK) {
        FTL_LOG(ERROR)
            << "AudioPlugDetector failed to activate watcher channel for \""
            << target.node_dir
            << "\" when attempting to start monitoring for new device nodes.  "
               "(mx_res "
            << mx_res << ")";
        return MediaResult::INTERNAL_ERROR;
      }

      // Step #5
      struct dirent* entry;
      DIR* d = fdopendir(dirfd.get());
      if (d == nullptr) {
        FTL_LOG(ERROR)
            << "AudioPlugDetector failed to enumerate initial contents of \""
            << target.node_dir
            << "\" when attempting to start monitoring for new device nodes.  "
               "(errno "
            << errno << ")";
        return MediaResult::INTERNAL_ERROR;
      }

      // We have transferred our file descriptor to 'd'.  We are not responsible
      // for closing it any more.
      (void)dirfd.release();

      // Process each non "." and ".." entry in the which already exists in the
      // dev-node directory.
      while ((entry = readdir(d)) != nullptr) {
        if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) {
          AddAudioDeviceLocked(entry->d_name, target);
        }
      }

      closedir(d);
    }

    error_cleanup.cancel();
  }

  return MediaResult::OK;
}

void AudioPlugDetector::Stop() {
  // Shutdown any channels we may have created in the past.
  ShutdownDispatcherChannels();

  // Enter the process lock, both to synchronize with any callbacks which may
  // already be in flight, and to clear the manager_ pointer to indicate that we
  // are no longer running.
  {
    mxtl::AutoLock process_lock(&process_lock_);
    manager_ = nullptr;
  }
}

mx_status_t AudioPlugDetector::ProcessChannel(::audio::DispatcherChannel* channel) {
  MX_DEBUG_ASSERT(channel != nullptr);
  mxtl::AutoLock process_lock(&process_lock_);

  // If we are shutting down, let the dispatcher framework know that it should
  // close this channel.  We are no longer interested in it.
  if (manager_ == nullptr)
    return MX_ERR_BAD_STATE;

  // Read the message from the watched channel.  It should contain the name of
  // a newly added device.  If we fail to read the channel, propagate the
  // error up to the dispatcher framework.  It will automatically take care of
  // closing the channel for us.
  char name[32];  // relative device node names should not need a max path
                  // buffer.
  uint32_t bytes;

  mx_status_t res = channel->Read(name, sizeof(name), &bytes);
  if (res == MX_OK) {
    FTL_DCHECK(channel->owner_ctx() != 0);
    const auto& watch_target =
        *(reinterpret_cast<WatchTarget*>(channel->owner_ctx()));

    // Manually null-terminate the filename.  The watcher channel does not do
    // this for us.
    name[mxtl::min<uint32_t>(sizeof(name) - 1, bytes)] = 0;

    AddAudioDeviceLocked(name, watch_target);
  }

  return res;
}

void AudioPlugDetector::AddAudioDeviceLocked(const char* node_name,
                                             const WatchTarget& watch_target) {
  FTL_DCHECK(manager_ != nullptr);

  static_assert(NAME_MAX <= 255,
                "NAME_MAX is growing!  Either update this static_assert, or "
                "consider switching to "
                "different way to generate this device node name.");
  char name[NAME_MAX + 1];
  snprintf(name, sizeof(name), "%s/%s", watch_target.node_dir, node_name);

  // Open the device node.
  ftl::UniqueFD dev_node(::open(
      name, watch_target.type == DevNodeType::AUDIO ? O_RDWR : O_RDONLY));
  if (!dev_node.is_valid()) {
    FTL_LOG(WARNING) << "AudioPlugDetector failed to open device node at \""
                     << name << "\". (" << strerror(errno) << " : " << errno
                     << ")";
    return;
  }

  AudioOutputPtr new_output;

  switch (watch_target.type) {
    case DevNodeType::AUDIO2_OUTPUT: {
      mx::channel channel;
      ssize_t res;

      res = ioctl_audio2_get_channel(dev_node.get(), channel.get_address());
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
                       << static_cast<uint>(watch_target.type)
                       << ") while attempting to add device.";
      return;
    }
  }

  if (new_output == nullptr) {
    FTL_LOG(WARNING) << "Failed to instantiate audio output for \"" << name
                     << "\"";
    return;
  }

  manager_->ScheduleMessageLoopTask([
    manager = manager_, output = new_output
  ]() { manager->AddOutput(output); });
}

}  // namespace audio
}  // namespace media
