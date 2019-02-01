// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "binder/Parcel.h"

#include <cassert>

namespace android {

int32_t Parcel::readInt32() const {
  assert(false && "not implemented");
  return 0;
}

const char* Parcel::readCString() const {
  assert(false && "not implemented");
  return nullptr;
}

int64_t Parcel::readInt64() const {
  assert(false && "not implemented");
  return 0;
}

float Parcel::readFloat() const {
  assert(false && "not implemented");
  return 0.0f;
}

double Parcel::readDouble() const {
  assert(false && "not implemented");
  return 0.0l;
}

status_t Parcel::writeInt32(int32_t value) {
  assert(false && "not implemented");
  return UNKNOWN_ERROR;
}

status_t Parcel::writeCString(const char* str) {
  assert(false && "not implemented");
  return UNKNOWN_ERROR;
}

status_t Parcel::writeInt64(int64_t value) {
  assert(false && "not implemented");
  return UNKNOWN_ERROR;
}

status_t Parcel::writeFloat(float value) {
  assert(false && "not implemented");
  return UNKNOWN_ERROR;
}

status_t Parcel::writeDouble(double value) {
  assert(false && "not implemented");
  return UNKNOWN_ERROR;
}

}  // namespace android
