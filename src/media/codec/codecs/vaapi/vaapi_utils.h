// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_CODEC_CODECS_VAAPI_VAAPI_UTILS_H_
#define SRC_MEDIA_CODEC_CODECS_VAAPI_VAAPI_UTILS_H_

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/fit/function.h>

#include <va/va.h>
#include <va/va_magma.h>

#include "media/gpu/h264_decoder.h"
#include "src/lib/fxl/macros.h"

class VADisplayWrapper {
 public:
  static bool InitializeSingleton(uint64_t vendor_id);
  static bool InitializeSingletonForTesting();

  static bool DestroySingleton();

  static VADisplayWrapper* GetSingleton();

  VADisplay display() { return display_; }

 private:
  bool Initialize();
  bool Destroy();
  magma_device_t magma_device_{};
  VADisplay display_{};
};

class ScopedConfigID {
 public:
  explicit ScopedConfigID(VAConfigID config_id) : id_(config_id) {}
  ~ScopedConfigID() { vaDestroyConfig(VADisplayWrapper::GetSingleton()->display(), id_); }

  VAConfigID id() const { return id_; }

 private:
  VAConfigID id_{VA_INVALID_ID};
  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(ScopedConfigID);
};

class ScopedBufferID {
 public:
  explicit ScopedBufferID(VABufferID buffer_id) : id_(buffer_id) {}
  ~ScopedBufferID() {
    if (id_ != VA_INVALID_ID)
      vaDestroyBuffer(VADisplayWrapper::GetSingleton()->display(), id_);
  }
  ScopedBufferID(ScopedBufferID&& other) noexcept {
    id_ = other.id_;
    other.id_ = VA_INVALID_ID;
  }

  ScopedBufferID& operator=(ScopedBufferID&& other) noexcept {
    id_ = other.id_;
    other.id_ = VA_INVALID_ID;
    return *this;
  }

  VABufferID id() const { return id_; }

 private:
  VABufferID id_{VA_INVALID_ID};
  FXL_DISALLOW_COPY_AND_ASSIGN(ScopedBufferID);
};

// For chromium code.
using ScopedVABuffer = ScopedBufferID;

class ScopedContextID {
 public:
  explicit ScopedContextID(VAContextID buffer_id) : id_(buffer_id) {}
  ~ScopedContextID() {
    if (id_ != VA_INVALID_ID)
      vaDestroyContext(VADisplayWrapper::GetSingleton()->display(), id_);
  }
  ScopedContextID(ScopedContextID&& other) noexcept {
    id_ = other.id_;
    other.id_ = VA_INVALID_ID;
  }

  ScopedContextID& operator=(ScopedContextID&& other) noexcept {
    id_ = other.id_;
    other.id_ = VA_INVALID_ID;
    return *this;
  }

  VAContextID id() const { return id_; }

 private:
  VAContextID id_{VA_INVALID_ID};
  FXL_DISALLOW_COPY_AND_ASSIGN(ScopedContextID);
};

class ScopedSurfaceID {
 public:
  explicit ScopedSurfaceID(VASurfaceID buffer_id) : id_(buffer_id) {}
  ~ScopedSurfaceID() {
    if (id_ != VA_INVALID_SURFACE)
      vaDestroySurfaces(VADisplayWrapper::GetSingleton()->display(), &id_, 1);
  }
  ScopedSurfaceID(ScopedSurfaceID&& other) noexcept {
    id_ = other.id_;
    other.id_ = VA_INVALID_SURFACE;
  }

  ScopedSurfaceID& operator=(ScopedSurfaceID&& other) noexcept {
    id_ = other.id_;
    other.id_ = VA_INVALID_SURFACE;
    return *this;
  }

  VASurfaceID id() const { return id_; }

  VASurfaceID release() {
    auto id = id_;
    id_ = VA_INVALID_SURFACE;
    return id;
  }

 private:
  VASurfaceID id_{VA_INVALID_SURFACE};
  FXL_DISALLOW_COPY_AND_ASSIGN(ScopedSurfaceID);
};

class VASurface {
 public:
  using ReleaseCB = fit::function<void(VASurfaceID)>;

  VASurface(VASurfaceID va_surface_id, const gfx::Size& size, unsigned int format,
            ReleaseCB release_cb);

  VASurface(const VASurface&) = delete;
  VASurface& operator=(const VASurface&) = delete;

  VASurfaceID id() const { return va_surface_id_; }
  const gfx::Size& size() const { return size_; }
  unsigned int format() const { return format_; }

  ~VASurface();

 private:
  const VASurfaceID va_surface_id_;
  const gfx::Size size_;
  const unsigned int format_;
  ReleaseCB release_cb_;
};

class ScopedImageID {
 public:
  explicit ScopedImageID(VAImageID image_id) : id_(image_id) {}
  ScopedImageID() = default;

  ~ScopedImageID() { DestroyImageIfNecessary(); }
  ScopedImageID(ScopedImageID&& other) noexcept {
    id_ = other.id_;
    other.id_ = VA_INVALID_ID;
  }

  ScopedImageID& operator=(ScopedImageID&& other) noexcept {
    DestroyImageIfNecessary();

    id_ = other.id_;
    other.id_ = VA_INVALID_ID;
    return *this;
  }

  VAImageID id() const { return id_; }

  VAImageID release() {
    auto id = id_;
    id_ = VA_INVALID_ID;
    return id;
  }

 private:
  void DestroyImageIfNecessary() {
    if (id_ != VA_INVALID_ID) {
      VAStatus status = vaDestroyImage(VADisplayWrapper::GetSingleton()->display(), id_);
      if (status != VA_STATUS_SUCCESS) {
        FX_LOGS(FATAL) << "vaDestroyImage failed: " << status;
      }
    }
    id_ = VA_INVALID_ID;
  }
  VAImageID id_{VA_INVALID_ID};
  FXL_DISALLOW_COPY_AND_ASSIGN(ScopedImageID);
};

std::vector<fuchsia::mediacodec::CodecDescription> GetCodecList();

// Copy the memory between arrays with checking the array size.
template <typename T, size_t N>
inline void SafeArrayMemcpy(T (&to)[N], const T (&from)[N]) {
  std::memcpy(to, from, sizeof(T[N]));
}

#endif  // SRC_MEDIA_CODEC_CODECS_VAAPI_VAAPI_UTILS_H_
