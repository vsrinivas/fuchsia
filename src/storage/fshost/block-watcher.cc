// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "block-watcher.h"

#include <fcntl.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <fuchsia/hardware/block/partition/c/fidl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <inttypes.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/unsafe.h>
#include <lib/fdio/watcher.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fzl/time.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/channel.h>
#include <lib/zx/process.h>
#include <lib/zx/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/device/block.h>
#include <zircon/errors.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <condition_variable>
#include <fstream>
#include <mutex>
#include <utility>

#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/string.h>
#include <fbl/string_buffer.h>
#include <fbl/string_piece.h>
#include <fbl/string_printf.h>
#include <fbl/unique_fd.h>
#include <fs-management/mount.h>
#include <gpt/gpt.h>
#include <zxcrypt/fdio-volume.h>

#include "src/storage/fshost/block-device-manager.h"
#include "src/storage/fshost/block-device.h"
#include "src/storage/fshost/fs-manager.h"
#include "src/storage/fshost/pkgfs-launcher.h"
#include "src/storage/minfs/minfs.h"

namespace devmgr {
namespace {

namespace fio = ::llcpp::fuchsia::io;

constexpr char kPathBlockDeviceRoot[] = "/dev/class/block";

// Signal that is set on the watcher channel we want to stop watching.
constexpr zx_signals_t kSignalWatcherPaused = ZX_USER_SIGNAL_0;

// Class used to pause/resume the block watcher.
BlockDeviceManager::Options GetDeviceManagerOptions(bool netboot) {
  std::ifstream file("/boot/config/fshost");
  BlockDeviceManager::Options options;
  if (file) {
    options = BlockDeviceManager::ReadOptions(file);
  } else {
    // fshost might be running from within a package (e.g. in tests).
    file = std::ifstream("/pkg/config/fshost");
    if (file) {
      options = BlockDeviceManager::ReadOptions(file);
    } else {
      options = BlockDeviceManager::DefaultOptions();
    }
  }
  if (netboot) {
    options.options[BlockDeviceManager::Options::kNetboot] = std::string();
  }
  return options;
}

}  // namespace

BlockWatcher::BlockWatcher(FsManager& fshost, FshostOptions options)
    : mounter_(fshost, options), device_manager_(GetDeviceManagerOptions(options.netboot)) {
  zx_status_t status = zx::event::create(0, &pause_event_);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to create block watcher pause event: "
                   << zx_status_get_string(status);
  }
}

void BlockWatcher::Run() {
  fbl::unique_fd dirfd(open(kPathBlockDeviceRoot, O_DIRECTORY | O_RDONLY));
  if (!dirfd) {
    FX_LOGS(ERROR) << "failed to open block device dir: " << strerror(errno);
    return;
  }
  fdio_cpp::FdioCaller caller(std::move(dirfd));
  auto cleanup = fbl::MakeAutoCall([this] {
    pause_event_.reset();
    pause_condition_.notify_all();
  });

  bool ignore_existing = false;
  while (true) {
    zx::channel watcher, server;
    zx_status_t status = zx::channel::create(0, &watcher, &server);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "failed to create watcher channel: " << zx_status_get_string(status);
      return;
    }

    auto mask = fio::WATCH_MASK_ALL;
    if (ignore_existing) {
      mask &= ~fio::WATCH_MASK_EXISTING;
    }
    auto result = fio::Directory::Call::Watch(caller.channel(), mask, 0, std::move(server));
    if (result.status() != ZX_OK) {
      FX_LOGS(ERROR) << "failed to send watch: " << zx_status_get_string(result.status());
      return;
    }
    if (result->s != ZX_OK) {
      FX_LOGS(ERROR) << "watch failed: " << zx_status_get_string(result->s);
      return;
    }

    {
      std::scoped_lock guard(lock_);
      if (is_paused_) {
        FX_LOGS(INFO) << "block watcher resumed";
        is_paused_ = false;
        pause_condition_.notify_all();
      }
    }

    // +1 for the NUL terminator at the end of the last name.
    uint8_t buf[fio::MAX_BUF + 1];
    fbl::Span buf_span(buf, std::size(buf) - 1);

    zx_signals_t signals;
    while ((signals = WaitForWatchMessages(watcher.borrow(), ignore_existing, buf_span)) ==
           ZX_CHANNEL_READABLE) {
      // Add an extra byte, so that ProcessWatchMessages can make C strings in the messages.
      buf_span = fbl::Span(buf_span.data(), buf_span.size() + 1);
      buf_span.back() = 0;
      if (ProcessWatchMessages(buf_span, caller.fd().get())) {
        ignore_existing = true;
      }

      // reset the buffer for the next read.
      buf_span = fbl::Span(buf, std::size(buf) - 1);
    }
    if (signals == kSignalWatcherPaused) {
      std::scoped_lock guard(lock_);
      is_paused_ = true;
      FX_LOGS(INFO) << "block watcher paused";
      pause_condition_.notify_all();
      // We were told to pause. Wait until we're resumed before re-starting the watch.
      while (pause_count_ > 0) {
        pause_condition_.wait(lock_);
      }
    } else {
      FX_LOGS(ERROR) << "watch failed with signal " << signals;
      break;
    }
  }
}

// Increment the pause count for the block watcher.
// This function will not return until the block watcher
// is no longer running.
// The block watcher will not receive any new device events while paused.
zx_status_t BlockWatcher::Pause() {
  auto guard = std::lock_guard(lock_);

  // Wait to resume before continuing.
  while (pause_count_ == 0 && is_paused_ && pause_event_)
    pause_condition_.wait(lock_);

  if (pause_count_ == std::numeric_limits<unsigned int>::max()) {
    return ZX_ERR_UNAVAILABLE;
  }
  if (!pause_event_) {
    // Refuse to pause -- the watcher won't actually stop.
    return ZX_ERR_BAD_STATE;
  }
  if (pause_count_ == 0) {
    // Tell the watcher to pause.
    zx_status_t status = pause_event_.signal(0, kSignalWatcherPaused);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "failed to set paused signal: " << zx_status_get_string(status);
      return status;
    }

    pause_count_++;
  } else {
    pause_count_++;
  }

  while (!is_paused_) {
    if (!pause_event_)
      return ZX_ERR_BAD_STATE;
    pause_condition_.wait(lock_);
  }

  return ZX_OK;
}

zx_status_t BlockWatcher::Resume() {
  auto guard = std::lock_guard(lock_);

  // Wait to pause before continuing.
  while (pause_count_ > 0 && !is_paused_ && pause_event_)
    pause_condition_.wait(lock_);

  if (pause_count_ == 0 || !pause_event_) {
    return ZX_ERR_BAD_STATE;
  }

  pause_count_--;
  if (pause_count_ == 0) {
    // Clear the pause signal.
    pause_event_.signal(kSignalWatcherPaused, 0);
    pause_condition_.notify_all();
  }

  // If this resume would cause the watcher to resume, wait until the watcher has actually resumed.
  // This helps avoid races in tests where they immediately create devices after resuming and
  // expecting fshost to have noticed.
  while (pause_count_ == 0 && is_paused_) {
    if (!pause_event_)
      return ZX_ERR_BAD_STATE;
    pause_condition_.wait(lock_);
  }
  return ZX_OK;
}

bool BlockWatcher::Callback(int dirfd, int event, const char* name) {
  if (event != fio::WATCH_EVENT_ADDED && event != fio::WATCH_EVENT_EXISTING &&
      event != fio::WATCH_EVENT_IDLE) {
    return false;
  }

  // Lock the block watcher, so any pause operations wait until after we're done.
  // Note that WATCH_EVENT_EXISTING is only received on the first run of the watcher,
  // so we don't need to worry about ignoring it on subsequent runs.
  std::lock_guard guard(lock_);
  if (event == fio::WATCH_EVENT_IDLE && pause_count_ > 0) {
    return true;
  }
  // If we lost the race and the watcher was paused sometime between
  // zx_object_wait_many returning and us acquiring the lock, bail out.
  if (pause_count_ > 0) {
    return false;
  }

  fbl::unique_fd device_fd(openat(dirfd, name, O_RDWR));
  if (!device_fd) {
    return false;
  }

  BlockDevice device(&mounter_, std::move(device_fd));
  zx_status_t status = device_manager_.AddDevice(device);
  if (status == ZX_ERR_NOT_SUPPORTED) {
    // The femu tests watch for the following message and will need updating if this changes.
    FX_LOGS(INFO) << "" << kPathBlockDeviceRoot << "/" << name << " ignored (not supported)";
  } else if (status != ZX_OK) {
    // There's not much we can do if this fails - we want to keep seeing future block device
    // events, so we just log loudly that we failed to do something.
    FX_LOGS(ERROR) << "" << kPathBlockDeviceRoot << "/" << name
                   << " failed: " << zx_status_get_string(status);
  }
  return false;
}

bool BlockWatcher::ProcessWatchMessages(fbl::Span<uint8_t> buf, int dirfd) {
  uint8_t* iter = buf.begin();
  bool did_receive_idle = false;
  while (iter + 2 <= buf.end()) {
    uint8_t event = *iter++;
    uint8_t name_len = *iter++;

    if (iter + name_len > buf.end()) {
      break;
    }

    // Save the first byte of the next message,
    // and null-terminate the name.
    uint8_t tmp = iter[name_len];
    iter[name_len] = 0;

    if (Callback(dirfd, event, reinterpret_cast<const char*>(iter))) {
      // We received a WATCH_EVENT_IDLE, and the watcher is paused.
      // Bail out early.
      return true;
    }
    if (event == fio::WATCH_EVENT_IDLE) {
      // WATCH_EVENT_IDLE - but the watcher is not paused. Finish processing this set of messages,
      // but return true once we're done to indicate that we should start checking for the pause
      // signal.
      did_receive_idle = true;
    }

    iter[name_len] = tmp;

    iter += name_len;
  }
  return did_receive_idle;
}

zx_signals_t BlockWatcher::WaitForWatchMessages(const zx::unowned_channel& watcher_chan,
                                                bool finished_startup, fbl::Span<uint8_t>& buf) {
  zx_status_t status;
  zx_wait_item_t wait_items[2] = {
      {
          .handle = watcher_chan->get(),
          .waitfor = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
          .pending = 0,
      },
      {
          .handle = pause_event_.get(),
          .waitfor = kSignalWatcherPaused,
          .pending = 0,
      },
  };

  // We only want to check for kSignalWatcherPaused if finished_startup is true.
  size_t wait_item_count = finished_startup ? 2 : 1;

  if ((status = zx_object_wait_many(wait_items, wait_item_count, zx::time::infinite().get())) !=
      ZX_OK) {
    FX_LOGS(ERROR) << "failed to wait_many: " << zx_status_get_string(status);
    return 0;
  }

  if (wait_items[1].pending & kSignalWatcherPaused) {
    return kSignalWatcherPaused;
  }
  if (wait_items[0].pending & ZX_CHANNEL_PEER_CLOSED) {
    return ZX_CHANNEL_PEER_CLOSED;
  }

  uint32_t read_len;
  status = zx_channel_read(watcher_chan->get(), 0, buf.begin(), nullptr,
                           static_cast<uint32_t>(buf.size()), 0, &read_len, nullptr);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to read from channel:" << zx_status_get_string(status);
    return 0;
  }
  buf = buf.subspan(0, read_len);
  return ZX_CHANNEL_READABLE;
}

fbl::RefPtr<fs::Service> BlockWatcherServer::Create(async_dispatcher* dispatcher,
                                                    BlockWatcher& watcher) {
  return fbl::MakeRefCounted<fs::Service>([dispatcher, &watcher](zx::channel chan) mutable {
    zx_status_t status = fidl::BindSingleInFlightOnly(
        dispatcher, std::move(chan),
        std::unique_ptr<BlockWatcherServer>(new BlockWatcherServer(watcher)));
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "failed to bind admin service:" << zx_status_get_string(status);
      return status;
    }
    return ZX_OK;
  });
}

void BlockWatcherServer::Pause(PauseCompleter::Sync& completer) {
  completer.Reply(watcher_.Pause());
}

void BlockWatcherServer::Resume(ResumeCompleter::Sync& completer) {
  completer.Reply(watcher_.Resume());
}

}  // namespace devmgr
