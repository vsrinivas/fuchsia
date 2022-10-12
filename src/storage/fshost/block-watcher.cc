// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "block-watcher.h"

#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <inttypes.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/unsafe.h>
#include <lib/fdio/watcher.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fzl/time.h>
#include <lib/syslog/cpp/macros.h>
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
#include <string_view>
#include <utility>

#include <fbl/algorithm.h>
#include <fbl/string.h>
#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <gpt/gpt.h>

#include "src/lib/storage/fs_management/cpp/mount.h"
#include "src/security/zxcrypt/client.h"
#include "src/storage/fshost/block-device-manager.h"
#include "src/storage/fshost/block-device.h"
#include "src/storage/fshost/fs-manager.h"
#include "src/storage/fshost/nand-device.h"

namespace fshost {
namespace {

namespace fio = fuchsia_io;

// Signal that is set on the watcher channel we want to stop watching.
constexpr zx_signals_t kSignalWatcherPaused = ZX_USER_SIGNAL_0;
constexpr zx_signals_t kSignalWatcherShutDown = ZX_USER_SIGNAL_1;

}  // namespace

BlockWatcher::BlockWatcher(FsManager& fshost, const fshost_config::Config* config)
    : mounter_(fshost, config), device_manager_(config, &fshost.inspect_manager()) {
  zx_status_t status = zx::event::create(0, &pause_event_);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to create block watcher pause event: "
                   << zx_status_get_string(status);
  }
}

void BlockWatcher::Run() {
  thread_ = std::thread([this] { Thread(); });
}

void BlockWatcher::Thread() {
  auto watchers = Watcher::CreateWatchers();
  if (watchers.empty()) {
    FX_LOGS(ERROR) << "failed to start any watchers";
    return;
  }
  auto cleanup = fit::defer([this] {
    pause_event_.reset();
    pause_condition_.notify_all();
  });

  while (true) {
    for (auto& watcher : watchers) {
      watcher.ReinitWatcher();
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
    uint8_t buf[fio::wire::kMaxBuf + 1];
    cpp20::span buf_span(buf, std::size(buf) - 1);

    zx_signals_t signals;
    Watcher* selected = nullptr;
    while ((signals = WaitForWatchMessages(watchers, buf_span, &selected)) == ZX_CHANNEL_READABLE) {
      // Add an extra byte, so that ProcessWatchMessages can make C strings in the messages.
      buf_span = cpp20::span(buf_span.data(), buf_span.size() + 1);
      buf_span.back() = 0;
      selected->ProcessWatchMessages(
          buf_span, [this](Watcher& watcher, int dirfd, fio::wire::WatchEvent event,
                           const char* name) { return Callback(watcher, dirfd, event, name); });

      // reset the buffer for the next read.
      buf_span = cpp20::span(buf, std::size(buf) - 1);
    }
    switch (signals) {
      case kSignalWatcherPaused: {
        std::scoped_lock guard(lock_);
        is_paused_ = true;
        FX_LOGS(INFO) << "block watcher paused";
        pause_condition_.notify_all();
        // We were told to pause. Wait until we're resumed before re-starting the watch.
        while (should_pause_) {
          pause_condition_.wait(lock_);
        }
        break;
      }
      case kSignalWatcherShutDown:
        return;
      default:
        FX_LOGS(ERROR) << "watch failed with signal " << signals;
        return;
    }
  }
}

void BlockWatcher::ShutDown() {
  if (thread_.joinable()) {
    {
      std::scoped_lock guard(lock_);
      should_pause_ = false;
    }
    pause_condition_.notify_all();
    pause_event_.signal(0, kSignalWatcherShutDown);
    thread_.join();
  }
}

// Increment the pause count for the block watcher.
// This function will not return until the block watcher
// is no longer running.
// The block watcher will not receive any new device events while paused.
zx_status_t BlockWatcher::Pause() {
  auto guard = std::lock_guard(lock_);

  // If we are told to pause, but we are already paused, it's an error.
  if (should_pause_) {
    return ZX_ERR_BAD_STATE;
  }

  // Wait to resume before continuing.
  while (!should_pause_ && is_paused_ && pause_event_)
    pause_condition_.wait(lock_);

  if (!pause_event_) {
    // Refuse to pause -- the watcher won't actually stop.
    return ZX_ERR_BAD_STATE;
  }

  should_pause_ = true;

  // Tell the watcher to pause.
  zx_status_t status = pause_event_.signal(0, kSignalWatcherPaused);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to set paused signal: " << zx_status_get_string(status);
    return status;
  }

  // Don't return from the pause call until the block watcher is actually paused.
  while (should_pause_ && !is_paused_) {
    if (!pause_event_)
      return ZX_ERR_BAD_STATE;
    pause_condition_.wait(lock_);
  }

  return ZX_OK;
}

zx_status_t BlockWatcher::Resume() {
  auto guard = std::lock_guard(lock_);

  // If we are told to resume, but we aren't actually paused, it's an error.
  if (!should_pause_) {
    return ZX_ERR_BAD_STATE;
  }

  // Wait to pause before continuing.
  while (should_pause_ && !is_paused_ && pause_event_)
    pause_condition_.wait(lock_);

  if (!pause_event_) {
    // Refuse to resume -- the watcher won't actually restart.
    return ZX_ERR_BAD_STATE;
  }

  should_pause_ = false;

  // Clear the pause signal.
  pause_event_.signal(kSignalWatcherPaused, 0);
  pause_condition_.notify_all();

  // If this resume would cause the watcher to resume, wait until the watcher has actually resumed.
  // This helps avoid races in tests where they immediately create devices after resuming and
  // expecting fshost to have noticed.
  while (!should_pause_ && is_paused_) {
    if (!pause_event_)
      return ZX_ERR_BAD_STATE;
    pause_condition_.wait(lock_);
  }
  return ZX_OK;
}

bool BlockWatcher::Callback(Watcher& watcher, int dirfd, fio::wire::WatchEvent event,
                            const char* name) {
  if (event != fio::wire::WatchEvent::kAdded && event != fio::wire::WatchEvent::kExisting &&
      event != fio::wire::WatchEvent::kIdle) {
    return false;
  }

  // Lock the block watcher, so any pause operations wait until after we're done.
  // Note that WATCH_EVENT_EXISTING is only received on the first run of the watcher,
  // so we don't need to worry about ignoring it on subsequent runs.
  std::lock_guard guard(lock_);
  if (event == fio::wire::WatchEvent::kIdle && should_pause_) {
    return true;
  }
  // If we lost the race and the watcher was paused sometime between
  // zx_object_wait_many returning and us acquiring the lock, bail out.
  if (should_pause_) {
    return false;
  }

  fbl::unique_fd device_fd(openat(dirfd, name, O_RDWR));
  if (!device_fd) {
    return false;
  }

  zx_status_t status = watcher.AddDevice(device_manager_, &mounter_, std::move(device_fd));
  if (status == ZX_ERR_NOT_SUPPORTED) {
    // The femu tests watch for the following message and will need updating if this changes.
    FX_LOGS(INFO) << "" << watcher.path() << "/" << name << " ignored (not supported)";
  } else if (status != ZX_OK) {
    // There's not much we can do if this fails - we want to keep seeing future block device
    // events, so we just log loudly that we failed to do something.
    FX_LOGS(ERROR) << "" << watcher.path() << "/" << name
                   << " failed: " << zx_status_get_string(status);
  }

  return false;
}

zx_signals_t BlockWatcher::WaitForWatchMessages(cpp20::span<Watcher> watchers,
                                                cpp20::span<uint8_t>& buf, Watcher** selected) {
  *selected = nullptr;
  zx_status_t status;
  std::vector<zx_wait_item_t> wait_items;
  bool can_pause = true;
  for (auto& watcher : watchers) {
    if (!watcher.ignore_existing()) {
      can_pause = false;
    }
    wait_items.emplace_back(zx_wait_item_t{
        .handle = watcher.borrow_watcher().channel()->get(),
        .waitfor = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
        .pending = 0,
    });
  }

  // We only want to check for kSignalWatcherPaused and kSignalWatcherShutDown if all watchers
  // are ignoring existing items.
  if (can_pause) {
    wait_items.emplace_back(zx_wait_item_t{
        .handle = pause_event_.get(),
        .waitfor = kSignalWatcherPaused | kSignalWatcherShutDown,
        .pending = 0,
    });
  }

  if ((status = zx_object_wait_many(wait_items.data(), wait_items.size(),
                                    zx::time::infinite().get())) != ZX_OK) {
    FX_LOGS(ERROR) << "failed to wait_many: " << zx_status_get_string(status);
    return 0;
  }

  if (can_pause) {
    if (wait_items.back().pending & kSignalWatcherShutDown) {
      return kSignalWatcherShutDown;
    }
    if (wait_items.back().pending & kSignalWatcherPaused) {
      return kSignalWatcherPaused;
    }
  }

  for (size_t i = 0; i < watchers.size(); ++i) {
    if (wait_items[i].pending & ZX_CHANNEL_PEER_CLOSED) {
      return ZX_CHANNEL_PEER_CLOSED;
    }

    if (wait_items[i].pending & ZX_CHANNEL_READABLE) {
      uint32_t read_len;
      status = watchers[i].borrow_watcher().channel()->read(
          0, buf.begin(), nullptr, static_cast<uint32_t>(buf.size()), 0, &read_len, nullptr);
      if (status != ZX_OK) {
        FX_LOGS(ERROR) << "failed to read from channel:" << zx_status_get_string(status);
        return 0;
      }
      *selected = &watchers[i];
      buf = buf.subspan(0, read_len);
      return ZX_CHANNEL_READABLE;
    }
  }

  ZX_ASSERT_MSG(false, "watcher got event but nothing is pending");
}

fbl::RefPtr<fs::Service> BlockWatcherServer::Create(async_dispatcher* dispatcher,
                                                    BlockWatcher& watcher) {
  return fbl::MakeRefCounted<fs::Service>(
      [dispatcher, &watcher](fidl::ServerEnd<fuchsia_fshost::BlockWatcher> chan) {
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

}  // namespace fshost
