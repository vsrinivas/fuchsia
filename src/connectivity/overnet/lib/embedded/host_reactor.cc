// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/embedded/host_reactor.h"

#include <poll.h>

#include <vector>

#include "src/connectivity/overnet/lib/environment/trace.h"

namespace overnet {

HostReactor::HostReactor() = default;

HostReactor::~HostReactor() {
  shutting_down_ = true;
  for (auto tmr : pending_timeouts_) {
    FireTimeout(tmr.second, overnet::Status::Cancelled());
  }
  auto fds = std::move(fds_);
  fds.clear();
}

void HostReactor::CancelIO(int fd) {
  if (auto it = fds_.find(fd); it != fds_.end()) {
    auto st = std::move(it->second);
    fds_.erase(it);
  }
}

void HostReactor::InitTimeout(overnet::Timeout* timeout,
                              overnet::TimeStamp when) {
  if (shutting_down_) {
    FireTimeout(timeout, overnet::Status::Cancelled());
    return;
  }

  OVERNET_TRACE(DEBUG) << "Set timer for " << when;

  *TimeoutStorage<overnet::TimeStamp>(timeout) = when;
  pending_timeouts_.emplace(when, timeout);
}

void HostReactor::CancelTimeout(overnet::Timeout* timeout,
                                overnet::Status status) {
  assert(!status.is_ok());

  auto rng = pending_timeouts_.equal_range(
      *TimeoutStorage<overnet::TimeStamp>(timeout));
  for (auto it = rng.first; it != rng.second; ++it) {
    if (it->second == timeout) {
      pending_timeouts_.erase(it);
      break;
    }
  }

  FireTimeout(timeout, status);
}

Status HostReactor::Run() {
  Execute([this] { return exit_status_.has_value(); });
  return *exit_status_;
}

void HostReactor::Step() {
  bool executed = false;
  Execute([&executed] {
    bool ret = executed;
    executed = true;
    return ret;
  });
}

template <class F>
void HostReactor::Execute(F exit_condition) {
  std::vector<int> gcfds;
  std::vector<pollfd> pollfds;
  std::vector<overnet::StatusCallback> cbs;

  while (!exit_condition()) {
    gcfds.clear();
    pollfds.clear();
    cbs.clear();

    const auto now = Now();

    // Run pending timers.
    bool ticked = false;
    while (!pending_timeouts_.empty() &&
           pending_timeouts_.begin()->first < now) {
      auto it = pending_timeouts_.begin();
      auto* timeout = it->second;
      pending_timeouts_.erase(it);
      FireTimeout(timeout, overnet::Status::Ok());
      ticked = true;
    }
    if (ticked) {
      continue;
    }

    // Setup polling datastructures.
    for (const auto& fd : fds_) {
      pollfd pfd;
      pfd.fd = fd.first;
      pfd.events = 0;
      pfd.revents = 0;
      if (!fd.second.on_read.empty()) {
        pfd.events |= POLLIN;
      }
      if (!fd.second.on_write.empty()) {
        pfd.events |= POLLOUT;
      }
      if (pfd.events != 0) {
        pollfds.push_back(pfd);
      } else {
        gcfds.push_back(fd.first);
      }
    }

    // Clear out dead fds.
    for (int fd : gcfds) {
      fds_.erase(fd);
    }

    // Check if there's a timer wake up for.
    timespec* next = nullptr;
    timespec next_store;
    if (!pending_timeouts_.empty()) {
      auto dt = pending_timeouts_.begin()->first - now;
      if (dt > overnet::TimeDelta::Zero()) {
        next = &next_store;
        next->tv_sec = dt.as_us() / 1000000;
        next->tv_nsec = 1000 * (dt.as_us() % 1000000);
      } else {
        next = &next_store;
        next->tv_sec = 0;
        next->tv_nsec = 0;
      }
    }

// Actually poll.
#if __linux__
    // Use ppoll for higher timing resolution
    int r = ppoll(pollfds.data(), pollfds.size(), next, nullptr);
#else
    // Use poll for compatibility
    int r = poll(pollfds.data(), pollfds.size(),
                 next ? next->tv_sec * 1000 + next->tv_nsec / 1000000 : -1);
#endif
    if (r < 0) {
      const int e = errno;
      if (e == EINTR) {
        // EINTR ==> just try again.
        continue;
      }
      if (!exit_status_.has_value()) {
        exit_status_ =
            overnet::Status(overnet::StatusCode::INVALID_ARGUMENT, strerror(e));
      }
      return;
    }

    // Reap incoming events.
    // We move callbacks into a secondary data structure
    // so that clients can reinstall OnRead/OnWrite callbacks
    // during their handling.
    if (r != 0) {
      for (const auto& pfd : pollfds) {
        if (pfd.revents == 0) {
          continue;
        }
        auto fd = fds_.find(pfd.fd);
        if (pfd.revents & POLLIN) {
          cbs.emplace_back(std::move(fd->second.on_read));
        }
        if (pfd.revents & POLLOUT) {
          cbs.emplace_back(std::move(fd->second.on_write));
        }
      }
      for (auto& cb : cbs) {
        cb(overnet::Status::Ok());
      }
    }
  }
}

}  // namespace overnet
