// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_CODEC_CODECS_VAAPI_VAAPI_UTILS_H_
#define SRC_MEDIA_CODEC_CODECS_VAAPI_VAAPI_UTILS_H_

#include <fuchsia/mediacodec/cpp/fidl.h>

#include <va/va.h>
#include <va/va_magma.h>

#include "media/gpu/h264_decoder.h"
#include "src/lib/fxl/macros.h"

class VADisplayWrapper {
 public:
  static bool InitializeSingleton(uint64_t vendor_id);
  static bool InitializeSingletonForTesting();

  static VADisplayWrapper* GetSingleton();

  VADisplay display() { return display_; }

 private:
  bool Initialize();
  magma_device_t magma_device_{};
  VADisplay display_{};
};

class ScopedConfigID {
 public:
  ScopedConfigID(VAConfigID config_id) : id_(config_id) {}
  ~ScopedConfigID() { vaDestroyConfig(VADisplayWrapper::GetSingleton()->display(), id_); }

  VAConfigID id() const { return id_; }

 private:
  VAConfigID id_;
  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(ScopedConfigID);
};

class ScopedBufferID {
 public:
  explicit ScopedBufferID(VABufferID buffer_id) : id_(buffer_id) {}
  ~ScopedBufferID() {
    if (id_)
      vaDestroyBuffer(VADisplayWrapper::GetSingleton()->display(), id_);
  }
  ScopedBufferID(ScopedBufferID&& other) noexcept {
    id_ = other.id_;
    other.id_ = 0;
  }

  ScopedBufferID& operator=(ScopedBufferID&& other) noexcept {
    id_ = other.id_;
    other.id_ = 0;
    return *this;
  }

  VABufferID id() const { return id_; }

 private:
  VABufferID id_;
  FXL_DISALLOW_COPY_AND_ASSIGN(ScopedBufferID);
};

class ScopedContextID {
 public:
  explicit ScopedContextID(VAContextID buffer_id) : id_(buffer_id) {}
  ~ScopedContextID() {
    if (id_)
      vaDestroyContext(VADisplayWrapper::GetSingleton()->display(), id_);
  }
  ScopedContextID(ScopedContextID&& other) noexcept {
    id_ = other.id_;
    other.id_ = 0;
  }

  ScopedContextID& operator=(ScopedContextID&& other) noexcept {
    id_ = other.id_;
    other.id_ = 0;
    return *this;
  }

  VAContextID id() const { return id_; }

 private:
  VAContextID id_;
  FXL_DISALLOW_COPY_AND_ASSIGN(ScopedContextID);
};

class ScopedSurfaceID {
 public:
  explicit ScopedSurfaceID(VASurfaceID buffer_id) : id_(buffer_id) {}
  ~ScopedSurfaceID() {
    if (id_)
      vaDestroySurfaces(VADisplayWrapper::GetSingleton()->display(), &id_, 1);
  }
  ScopedSurfaceID(ScopedSurfaceID&& other) noexcept {
    id_ = other.id_;
    other.id_ = 0;
  }

  ScopedSurfaceID& operator=(ScopedSurfaceID&& other) noexcept {
    id_ = other.id_;
    other.id_ = 0;
    return *this;
  }

  VASurfaceID id() const { return id_; }

  VASurfaceID release() {
    auto id = id_;
    id_ = 0;
    return id;
  }

 private:
  VASurfaceID id_;
  FXL_DISALLOW_COPY_AND_ASSIGN(ScopedSurfaceID);
};

std::vector<fuchsia::mediacodec::CodecDescription> GetCodecList();

#endif  // SRC_MEDIA_CODEC_CODECS_VAAPI_VAAPI_UTILS_H_
