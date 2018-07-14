// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/public/lib/fostr/zx_types.h"

#include <iomanip>

namespace zx {
namespace {

void insert_handle_koid(std::ostream& os, zx_handle_t handle) {
  if (handle == ZX_HANDLE_INVALID) {
    os << "<invalid>";
    return;
  }

  zx_info_handle_basic_t info;
  zx_status_t status = zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info,
                                          sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    os << "<get info failed>";
  }

  os << "koid 0x" << std::hex << info.koid << std::dec;
}

void insert_handle_koid_pair(std::ostream& os, zx_handle_t handle) {
  if (handle == ZX_HANDLE_INVALID) {
    os << "<invalid>";
    return;
  }

  zx_info_handle_basic_t info;
  zx_status_t status = zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info,
                                          sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    os << "<get info failed>";
  }

  os << "koid 0x" << std::hex << info.koid << " <-> 0x" << info.related_koid
     << std::dec;
}

}  // namespace

std::ostream& operator<<(std::ostream& os, const zx::object_base& value) {
  insert_handle_koid(os, value.get());
  return os;
}

std::ostream& operator<<(std::ostream& os, const zx::channel& value) {
  insert_handle_koid_pair(os, value.get());
  return os;
}

std::ostream& operator<<(std::ostream& os, const zx::eventpair& value) {
  insert_handle_koid_pair(os, value.get());
  return os;
}

std::ostream& operator<<(std::ostream& os, const zx::fifo& value) {
  insert_handle_koid_pair(os, value.get());
  return os;
}

std::ostream& operator<<(std::ostream& os, const zx::process& value) {
  char name[ZX_MAX_NAME_LEN];
  zx_status_t status =
      zx_object_get_property(value.get(), ZX_PROP_NAME, name, sizeof(name));

  if (status == ZX_OK) {
    return os << name;
  } else {
    insert_handle_koid(os, value.get());
    return os;
  }
}

std::ostream& operator<<(std::ostream& os, const zx::socket& value) {
  insert_handle_koid_pair(os, value.get());
  return os;
}

std::ostream& operator<<(std::ostream& os, const zx::thread& value) {
  char name[ZX_MAX_NAME_LEN];
  zx_status_t status =
      zx_object_get_property(value.get(), ZX_PROP_NAME, name, sizeof(name));

  if (status == ZX_OK) {
    return os << name;
  } else {
    insert_handle_koid(os, value.get());
    return os;
  }
}

std::ostream& operator<<(std::ostream& os, const zx::duration& value) {
  if (value == zx::duration::infinite()) {
    return os << "<infinite>";
  }

  uint64_t s = value.to_nsecs();

  if (s == 0) {
    return os << "0";
  }

  int64_t ns = s % 1000;
  s /= 1000;
  int64_t us = s % 1000;
  s /= 1000;
  int64_t ms = s % 1000;
  s /= 1000;

  return os << s << "." << std::setw(3) << std::setfill('0') << ms << ","
            << std::setw(3) << std::setfill('0') << us << "," << std::setw(3)
            << std::setfill('0') << ns;
}

std::ostream& operator<<(std::ostream& os, const zx::vmo& value) {
  insert_handle_koid(os, value.get());

  uint64_t size;
  if (value.get_size(&size) == ZX_OK) {
    os << ", " << size << " bytes";
  }

  return os;
}

}  // namespace zx
