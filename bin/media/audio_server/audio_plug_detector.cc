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

    // Size our watcher vector.
    watch_targets_.reserve(countof(WATCH_TARGETS));

    // For each watch target...
    // 1) Open the directory where dev-nodes get published.  Add this dir fd to
    //    our vector of active watcher targets so we can use it with openat
    //    later on.
    // 2) Create a DispatcherChannel channel using a pointer to the watcher
    //    target owner ctx (so we know what type of device we need to attach to,
    //    and have access to the dirfd)
    // 3) Activate the DispatcherChannel, binding it to ourselves, and obtaining
    //    the other end of the channel in the process.
    // 4) Give the other end of our newly created dispatcher channel to the VFS
    //    and tell it to watch for existing or added device nodes.
    for (const auto& target : WATCH_TARGETS) {
      // Step #1
      ftl::UniqueFD tmp(::open(target.node_dir, O_RDONLY | O_DIRECTORY));
      if (!tmp.is_valid()) {
        FTL_LOG(ERROR) << "AudioPlugDetector failed to open \""
                       << target.node_dir
                       << "\" when attempting to start monitoring for new "
                          "device nodes.  (errno "
                       << errno << ")";
        return MediaResult::NOT_FOUND;
      }

      watch_targets_.emplace_back(std::move(tmp), target.type);
      auto& wt = watch_targets_.back();

      // Step #2
      auto dispatcher = ::audio::DispatcherChannelAllocator::New(
          reinterpret_cast<uintptr_t>(&wt));
      if (dispatcher == nullptr) {
        FTL_LOG(ERROR)
            << "AudioPlugDetector failed to create DispatcherChannel for \""
            << target.node_dir
            << "\" when attempting to start monitoring for new device nodes.";
        return MediaResult::INSUFFICIENT_RESOURCES;
      }

      // Step #3
      mx_status_t mx_res;
      mx::channel watcher_channel;
      mx_res = dispatcher->Activate(mxtl::WrapRefPtr(this), &watcher_channel);
      if (mx_res != MX_OK) {
        FTL_LOG(ERROR)
            << "AudioPlugDetector failed to activate watcher channel for \""
            << target.node_dir
            << "\" when attempting to start monitoring for new device nodes.  "
               "(mx_res "
            << mx_res << ")";
        return MediaResult::INTERNAL_ERROR;
      }

      // Step #4
      vfs_watch_dir_t wd;
      wd.channel = watcher_channel.release();
      wd.mask = VFS_WATCH_MASK_ADDED | VFS_WATCH_MASK_EXISTING;
      wd.options = 0;
      ssize_t res;
      res = ioctl_vfs_watch_dir_v2(wt.dirfd.get(), &wd);
      if (res < 0) {
        FTL_LOG(ERROR) << "AudioPlugDetector failed to create watcher for \""
                       << target.node_dir
                       << "\" when attempting to start monitoring for new "
                          "device nodes.  (res "
                       << res << ")";
        dispatcher->Deactivate(false);
        mx_handle_close(wd.channel);
        return MediaResult::INTERNAL_ERROR;
      }
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

mx_status_t AudioPlugDetector::ProcessChannel(
    ::audio::DispatcherChannel* channel) {
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
  uint32_t bytes;
  uint8_t watcher_msg_buf[VFS_WATCH_MSG_MAX];
  mx_status_t res = channel->Read(watcher_msg_buf,
                                  sizeof(watcher_msg_buf),
                                  &bytes);
  if (res != MX_OK)
    return res;

  FTL_DCHECK(channel->owner_ctx() != 0);
  const auto& watch_target =
      *(reinterpret_cast<WatchTarget*>(channel->owner_ctx()));

  uint8_t* msg = watcher_msg_buf;

  while (bytes >= 2) {
      uint8_t event   = *msg++;
      uint8_t namelen = *msg++;
      if (bytes < (namelen + 2u))
          return MX_ERR_BAD_STATE;

      if (namelen &&
          ((event == VFS_WATCH_EVT_ADDED) || (event == VFS_WATCH_EVT_EXISTING))) {
        char name_buf[256];
        ::memcpy(name_buf, msg, namelen);
        name_buf[namelen + 1u] = 0;
        AddAudioDeviceLocked(name_buf, watch_target);
      }

      msg += namelen;
      bytes -= (namelen + 2u);
  }

  return res;
}

void AudioPlugDetector::AddAudioDeviceLocked(const char* node_name,
                                             const WatchTarget& watch_target) {
  FTL_DCHECK(manager_ != nullptr);

  // Open the device node.
  ftl::UniqueFD dev_node(
      ::openat(watch_target.dirfd.get(),
               node_name,
               watch_target.type == DevNodeType::AUDIO ? O_RDWR : O_RDONLY));
  if (!dev_node.is_valid()) {
    FTL_LOG(WARNING) << "AudioPlugDetector failed to open device node at \""
                     << node_name << "\". ("
                     << strerror(errno) << " : " << errno
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
        FTL_DLOG(WARNING) << "Failed to get device type for \"" << node_name
                          << "\" (res " << res << ")";
        return;
      }

      if (device_type != AUDIO_TYPE_SINK) {
        FTL_DLOG(INFO) << "Unsupported USB audio device (type " << device_type
                       << ") at \"" << node_name << "\".  Skipping";
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
    FTL_LOG(WARNING) << "Failed to instantiate audio output for \"" << node_name
                     << "\"";
    return;
  }

  manager_->ScheduleMessageLoopTask([
    manager = manager_, output = new_output
  ]() { manager->AddOutput(output); });
}

}  // namespace audio
}  // namespace media
