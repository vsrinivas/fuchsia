// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_plug_detector_impl.h"

#include <fcntl.h>
#include <fuchsia/hardware/audio/cpp/fidl.h>
#include <lib/fdio/fdio.h>
#include <lib/fit/defer.h>
#include <lib/zx/channel.h>
#include <zircon/compiler.h>

#include <fbl/unique_fd.h>
#include <trace/event.h>

#include "src/lib/syslog/cpp/logger.h"
#include "src/media/audio/audio_core/reporter.h"

namespace media::audio {

static const struct {
  const char* path;
  bool is_input;
} AUDIO_DEVNODES[] = {
    {.path = "/dev/class/audio-output", .is_input = false},
    {.path = "/dev/class/audio-input", .is_input = true},
};

zx_status_t AudioPlugDetectorImpl::Start(Observer observer) {
  TRACE_DURATION("audio", "AudioPlugDetectorImpl::Start");
  // Start should only be called once.
  FX_DCHECK(watchers_.empty());
  FX_DCHECK(!observer_);
  FX_DCHECK(observer);

  observer_ = std::move(observer);

  // If we fail to set up monitoring for any of our target directories,
  // automatically stop monitoring all sources of device nodes.
  auto error_cleanup = fit::defer([this]() { Stop(); });

  // Create our watchers.
  for (const auto& devnode : AUDIO_DEVNODES) {
    auto watcher = fsl::DeviceWatcher::Create(
        devnode.path, [this, is_input = devnode.is_input](int dir_fd, const std::string& filename) {
          AddAudioDevice(dir_fd, filename, is_input);
        });

    if (watcher == nullptr) {
      FX_LOGS(ERROR) << "AudioPlugDetectorImpl failed to create DeviceWatcher for \""
                     << devnode.path << "\".";
      return ZX_ERR_NO_MEMORY;
    }

    watchers_.emplace_back(std::move(watcher));
  }

  error_cleanup.cancel();

  return ZX_OK;
}

void AudioPlugDetectorImpl::Stop() {
  TRACE_DURATION("audio", "AudioPlugDetectorImpl::Stop");
  observer_ = nullptr;
  watchers_.clear();
}

void AudioPlugDetectorImpl::AddAudioDevice(int dir_fd, const std::string& name, bool is_input) {
  TRACE_DURATION("audio", "AudioPlugDetectorImpl::AddAudioDevice");
  if (!observer_) {
    return;
  }

  // Open the device node.
  //
  // TODO(35145): Remove blocking 'openat' from the main thread. fdio_open_at is probably what we
  // want, but we'll need a version of DeviceWatcher that operates on fuchsia::io::Directory
  // handles instead of file descriptors.
  fbl::unique_fd dev_node(openat(dir_fd, name.c_str(), O_RDONLY));
  if (!dev_node.is_valid()) {
    REP(FailedToOpenDevice(name, is_input, errno));
    FX_LOGS(ERROR) << "AudioPlugDetectorImpl failed to open device node at \"" << name << "\". ("
                   << strerror(errno) << " : " << errno << ")";
    return;
  }

  // Obtain the FDIO device channel, wrap it in a sync proxy, use that to get the stream channel.
  zx_status_t res;
  zx::channel dev_channel;
  res = fdio_get_service_handle(dev_node.release(), dev_channel.reset_and_get_address());
  if (res != ZX_OK) {
    REP(FailedToObtainFdioServiceChannel(name, is_input, res));
    FX_PLOGS(ERROR, res) << "Failed to obtain FDIO service channel to audio "
                         << (is_input ? "input" : "output");
    return;
  }

  // Obtain the stream channel
  auto device =
      fidl::InterfaceHandle<fuchsia::hardware::audio::Device>(std::move(dev_channel)).Bind();
  device.set_error_handler([name, is_input](zx_status_t res) {
    REP(FailedToObtainStreamChannel(name, is_input, res));
    FX_PLOGS(ERROR, res) << "Failed to open channel to audio " << (is_input ? "input" : "output");
  });
  device->GetChannel([d = std::move(device), this, is_input,
                      name](::fidl::InterfaceRequest<fuchsia::hardware::audio::StreamConfig> req) {
    observer_(req.TakeChannel(), name, is_input);
  });
}

}  // namespace media::audio
