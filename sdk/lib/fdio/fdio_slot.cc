// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sdk/lib/fdio/fdio_slot.h"

#include "sdk/lib/fdio/internal.h"

fbl::RefPtr<fdio> fdio_slot::get() {
  fdio_t** ptr = std::get_if<fdio_t*>(&inner_);
  if (ptr != nullptr) {
    return fbl::RefPtr(*ptr);
  }
  return nullptr;
}

fbl::RefPtr<fdio> fdio_slot::release() {
  fdio_t** ptr = std::get_if<fdio_t*>(&inner_);
  if (ptr != nullptr) {
    fbl::RefPtr<fdio> io = fbl::ImportFromRawPtr(*ptr);
    inner_ = available{};
    return io;
  }
  return nullptr;
}

bool fdio_slot::try_set(fbl::RefPtr<fdio> io) {
  if (std::holds_alternative<available>(inner_)) {
    inner_ = fbl::ExportToRawPtr(&io);
    return true;
  }
  return false;
}

fbl::RefPtr<fdio> fdio_slot::replace(fbl::RefPtr<fdio> io) {
  auto previous = std::exchange(inner_, fbl::ExportToRawPtr(&io));
  fdio_t** ptr = std::get_if<fdio_t*>(&previous);
  if (ptr != nullptr) {
    return fbl::ImportFromRawPtr(*ptr);
  }
  return nullptr;
}

std::optional<void (fdio_slot::*)()> fdio_slot::try_reserve() {
  if (std::holds_alternative<available>(inner_)) {
    inner_ = reserved{};
    return &fdio_slot::release_reservation;
  }
  return std::nullopt;
}

bool fdio_slot::try_fill(fbl::RefPtr<fdio> io) {
  if (std::holds_alternative<reserved>(inner_)) {
    inner_ = fbl::ExportToRawPtr(&io);
    return true;
  }
  return false;
}

void fdio_slot::release_reservation() {
  if (std::holds_alternative<reserved>(inner_)) {
    inner_ = available{};
  }
}
