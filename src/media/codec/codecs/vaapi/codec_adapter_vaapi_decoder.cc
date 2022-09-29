// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_adapter_vaapi_decoder.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/defer.h>
#include <lib/stdcompat/span.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/assert.h>
#include <zircon/status.h>

#include <condition_variable>
#include <mutex>
#include <optional>
#include <unordered_map>

#include <fbl/algorithm.h>
#include <safemath/checked_math.h>
#include <safemath/safe_conversions.h>
#include <va/va_drmcommon.h>

#include "geometry.h"
#include "h264_accelerator.h"
#include "media/gpu/h264_decoder.h"
#include "media/gpu/vp9_decoder.h"
#include "vp9_accelerator.h"

// This class manages output buffers when the client selects a linear buffer output. Since the
// output is linear the client will have to deswizzle the output from the decoded picture buffer
// (DPB) meaning that we can't directly share the output with the client. The manager will be
// responsible for creating the DPB surfaces used by the decoder and reconstructing them when a mid
// stream configuration change is required. This buffer manager will also be responsible for copying
// the output from the DBPs to the CodecBuffers the client provides us.
class LinearBufferManager : public SurfaceBufferManager {
 public:
  LinearBufferManager(std::mutex& codec_lock, CodecFailureCallback failure_callback)
      : SurfaceBufferManager(codec_lock, std::move(failure_callback)) {}
  ~LinearBufferManager() override = default;

  void AddBuffer(const CodecBuffer* buffer) override { output_buffer_pool_.AddBuffer(buffer); }

  void RecycleBuffer(const CodecBuffer* buffer) override {
    LinearOutput local_output;
    {
      std::lock_guard<std::mutex> guard(codec_lock_);
      ZX_DEBUG_ASSERT(in_use_by_client_.find(buffer) != in_use_by_client_.end());
      local_output = std::move(in_use_by_client_[buffer]);
      in_use_by_client_.erase(buffer);
    }
    // ~ local_output, which may trigger a buffer free callback.
  }

  void DeconfigureBuffers() override {
    // First drop all the buffers that are currently in use by the client, this will return them
    // back to the ouput_buffer_pool_.
    {
      std::map<const CodecBuffer*, LinearOutput> to_drop;
      {
        std::lock_guard<std::mutex> lock(codec_lock_);
        std::swap(to_drop, in_use_by_client_);
      }
    }
    // ~to_drop

    ZX_DEBUG_ASSERT(!output_buffer_pool_.has_buffers_in_use());

    // Once all the buffers have been returned to the bool, deallocate them
    output_buffer_pool_.Reset(false);
  }

  scoped_refptr<VASurface> GetDPBSurface() override {
    uint64_t surface_generation;
    VASurfaceID surface_id;
    gfx::Size pic_size;

    {
      std::lock_guard<std::mutex> guard(surface_lock_);
      if (dpb_surfaces_.empty()) {
        return {};
      }
      surface_id = dpb_surfaces_.back().release();
      dpb_surfaces_.pop_back();
      surface_generation = surface_generation_;
      pic_size = dpb_surface_size_;
    }

    // Called once the reference count of the surface hits 0, meaning that it is no longer in use by
    // the decoder. If the surface_generation_ is the same as when the surface was created, then we
    // transfer ownership back to the |dpb_surfaces_| data structure. If however the surface
    // generation parameters have changed then we destroy the surface by calling
    // vaDestroySurfaces().
    VASurface::ReleaseCB release_cb = [this, surface_generation](VASurfaceID surface_id) {
      std::lock_guard lock(surface_lock_);
      if (surface_generation_ == surface_generation) {
        dpb_surfaces_.emplace_back(surface_id);
      } else {
        auto status =
            vaDestroySurfaces(VADisplayWrapper::GetSingleton()->display(), &surface_id, 1);

        if (status != VA_STATUS_SUCCESS) {
          FX_SLOG(WARNING, "vaDestroySurfaces failed", KV("error_str", vaErrorStr(status)));
        }
      }
    };

    return std::make_shared<VASurface>(surface_id, pic_size, VA_RT_FORMAT_YUV420,
                                       std::move(release_cb));
  }

  std::optional<std::pair<const CodecBuffer*, uint32_t>> ProcessOutputSurface(
      scoped_refptr<VASurface> va_surface) override {
    const CodecBuffer* buffer = output_buffer_pool_.AllocateBuffer();

    if (!buffer) {
      return std::nullopt;
    }

    // If any errors happen, release the buffer back into the pool unless canceled.
    auto release_buffer = fit::defer([&]() { output_buffer_pool_.FreeBuffer(buffer->base()); });

    // Even though there can be surfaces of varying different resolutions, we can always be
    // guaranteed that the current surface will be able to hold the current frame. And we should
    // base all calculations based on the current surface and not |dpb_surface_size_|.
    const auto& surface_size = va_surface->size();

    // Calculate the size of the Y and UV planes for the given surface. We will be populating these
    // values into various VADRMPRIMESurfaceDescriptor fields which are of type uint32_t. Ensure
    // that the values can be represented as a uint32_t
    //
    // We calculating the |aligned_stride_checked| we use the width of the current surface. The
    // picture width might be smaller, but we still need the stride of the actual surface in order
    // to calculate the correct size. For the Y and UV plane height we use |coded_picture_size_|
    // which holds the current picture size since the height is based on the current picture size
    // and the width is based on the overall surface stride.
    //
    // TODO(stefanbossbaly): We should consider making the created surface only as big as needed to
    // hold the image generated by |coded_picture_size_| instead of the the current size of the
    // surface. This would require that we inform the client that update |bytes_per_row| to reflect
    // the current size of the smaller surface. The below method works fine, we do end up create an
    // image backed by a surface that can be larger than |coded_picture_size_| which will increase
    // the amount of time to deswizzle and copy to our VMO since we will end up copying junk data in
    // the DPB that was not a part of the current decode operation.
    auto aligned_stride_checked = safemath::MakeCheckedNum(surface_size.width()).Cast<uint32_t>();
    auto aligned_y_height = safemath::checked_cast<uint32_t>(coded_picture_size_.height());
    auto aligned_uv_height = safemath::checked_cast<uint32_t>(coded_picture_size_.height()) / 2u;
    auto y_plane_size_checked =
        safemath::CheckMul(aligned_stride_checked, aligned_y_height).Cast<uint32_t>();
    auto uv_plane_size_checked =
        safemath::CheckMul(aligned_stride_checked, aligned_uv_height).Cast<uint32_t>();
    auto total_plane_size_checked = (y_plane_size_checked + uv_plane_size_checked).Cast<uint32_t>();

    uint32_t aligned_stride, y_plane_size, total_plane_size;
    if (!aligned_stride_checked.IsValid()) {
      FX_SLOG(ERROR, "Ouput stride can not be represented as uint32_t");
      return std::nullopt;
    }
    aligned_stride = aligned_stride_checked.ValueOrDie();

    if (!y_plane_size_checked.IsValid()) {
      FX_SLOG(ERROR, "Y-Plane size can not be represented as uint32_t");
      return std::nullopt;
    }
    y_plane_size = y_plane_size_checked.ValueOrDie();

    if (!total_plane_size_checked.IsValid()) {
      FX_SLOG(ERROR, "Total plane size can not be represented as uint32_t");
      return std::nullopt;
    }
    total_plane_size = total_plane_size_checked.ValueOrDie();

    ZX_ASSERT_MSG(buffer->size() >= total_plane_size,
                  "Picture size (%u bytes) exceeds buffer size (%zu bytes)", total_plane_size,
                  buffer->size());

    zx::vmo vmo_dup;
    zx_status_t zx_status = buffer->vmo().duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_dup);
    if (zx_status != ZX_OK) {
      FX_SLOG(ERROR, "Failed to duplicate vmo", KV("error_str", zx_status_get_string(zx_status)));
      return std::nullopt;
    }

    // For the moment we use DRM_PRIME_2 to represent VMOs.
    // To specify the destination VMO, we need two VASurfaceAttrib, one to set the
    // VASurfaceAttribMemoryType to VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2 and one for the
    // VADRMPRIMESurfaceDescriptor.
    VADRMPRIMESurfaceDescriptor ext_attrib{};
    VASurfaceAttrib attrib[2] = {
        {.type = VASurfaceAttribMemoryType,
         .flags = VA_SURFACE_ATTRIB_SETTABLE,
         .value = {.type = VAGenericValueTypeInteger,
                   .value = {.i = VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2}}},
        {.type = VASurfaceAttribExternalBufferDescriptor,
         .flags = VA_SURFACE_ATTRIB_SETTABLE,
         .value = {.type = VAGenericValueTypePointer, .value = {.p = &ext_attrib}}},
    };

    // VADRMPRIMESurfaceDescriptor width will match the output stride instead of the coded width.
    ext_attrib.width = surface_size.width();
    ext_attrib.height = coded_picture_size_.height();
    ext_attrib.fourcc = VA_FOURCC_NV12;  // 2 plane YCbCr
    ext_attrib.num_objects = 1;
    ext_attrib.objects[0].fd = vmo_dup.release();
    ext_attrib.objects[0].drm_format_modifier = fuchsia::sysmem::FORMAT_MODIFIER_LINEAR;
    ext_attrib.objects[0].size = static_cast<uint32_t>(buffer->size());
    ext_attrib.num_layers = 1;
    ext_attrib.layers[0].drm_format = make_fourcc('N', 'V', '1', '2');
    ext_attrib.layers[0].num_planes = 2;

    // Y plane
    ext_attrib.layers[0].object_index[0] = 0;
    ext_attrib.layers[0].pitch[0] = aligned_stride;
    ext_attrib.layers[0].offset[0] = 0;

    // UV Plane
    ext_attrib.layers[0].object_index[1] = 0;
    ext_attrib.layers[0].pitch[1] = aligned_stride;
    ext_attrib.layers[0].offset[1] = y_plane_size;

    VAStatus status = vaSyncSurface(VADisplayWrapper::GetSingleton()->display(), va_surface->id());

    if (status != VA_STATUS_SUCCESS) {
      // Get more information of the error, if possible. vaQuerySurfaceError can only be called iff
      // vaSyncSurface returns VA_STATUS_ERROR_DECODING_ERROR. If that is the case then we call
      // vaQuerySurfaceError which will return an array of macroblock error structures which tells
      // us what offending macroblocks caused the error and what type of error was encountered.
      bool detailed_query = false;
      if (status == VA_STATUS_ERROR_DECODING_ERROR) {
        VASurfaceDecodeMBErrors* decode_mb_errors;
        VAStatus query_status = vaQuerySurfaceError(
            VADisplayWrapper::GetSingleton()->display(), va_surface->id(),
            VA_STATUS_ERROR_DECODING_ERROR, reinterpret_cast<void**>(&decode_mb_errors));

        if (query_status == VA_STATUS_SUCCESS) {
          detailed_query = true;
          FX_SLOG(ERROR, "SyncSurface failed due to the following macroblock errors ...");

          // Limit the amount of errors we can display, just to ensure we don't enter an infinite
          // loop or spam the log with messages
          static constexpr uint32_t kMaxMBErrors = 10u;
          uint32_t mb_error_count = 0u;

          while ((decode_mb_errors != nullptr) && (decode_mb_errors->status != -1) &&
                 (mb_error_count < kMaxMBErrors)) {
            FX_SLOG(ERROR, "SyncSurface a macroblock error",
                    KV("decode_error", (decode_mb_errors->decode_error_type == VADecodeSliceMissing)
                                           ? "VADecodeSliceMissing"
                                           : "VADecodeMBError"),
                    KV("start_mb", decode_mb_errors->start_mb),
                    KV("end_mb", decode_mb_errors->end_mb), KV("num_mb", decode_mb_errors->num_mb));
            decode_mb_errors++;
            mb_error_count++;
          }
        }
      }

      // If the error was not VA_STATUS_ERROR_DECODING_ERROR or vaQuerySurfaceError returned an
      // error, just log a generic error message.
      if (!detailed_query) {
        FX_SLOG(ERROR, "SyncSurface failed", KV("error_str", vaErrorStr(status)));
      }

      return std::nullopt;
    }

    // Create the surface backed by the destination VMO. Since we are using
    // VADRMPRIMESurfaceDescriptor, the width and height of the vaCreateSurfaces() call will be
    // overridden by |ext_attrib.width| and |ext_attrib.height|.
    VASurfaceID processed_surface_id;
    status =
        vaCreateSurfaces(VADisplayWrapper::GetSingleton()->display(), VA_RT_FORMAT_YUV420,
                         ext_attrib.width, ext_attrib.height, &processed_surface_id, 1, attrib, 2);
    if (status != VA_STATUS_SUCCESS) {
      FX_SLOG(WARNING, "vaCreateSurfaces failed", KV("error_str", vaErrorStr(status)));
      return std::nullopt;
    }

    ScopedSurfaceID processed_surface(processed_surface_id);

    // Set up a VAImage for the destination VMO.
    VAImage image;
    status =
        vaDeriveImage(VADisplayWrapper::GetSingleton()->display(), processed_surface.id(), &image);
    if (status != VA_STATUS_SUCCESS) {
      FX_SLOG(WARNING, "vaDeriveImage failed", KV("error_str", vaErrorStr(status)));
      return std::nullopt;
    }

    {
      ScopedImageID scoped_image(image.image_id);

      // Copy from potentially-tiled surface to output surface. Intel decoders only
      // support writing to Y-tiled textures, so this copy is necessary for linear
      // output.
      status = vaGetImage(VADisplayWrapper::GetSingleton()->display(), va_surface->id(), 0, 0,
                          surface_size.width(), coded_picture_size_.height(), scoped_image.id());
      if (status != VA_STATUS_SUCCESS) {
        FX_SLOG(WARNING, "vaGetImage failed", KV("error_str", vaErrorStr(status)));
        return std::nullopt;
      }
    }
    // ~processed_surface: Clean up the image; the data was already copied to the destination VMO
    // above.

    {
      std::lock_guard<std::mutex> guard(codec_lock_);
      ZX_DEBUG_ASSERT(in_use_by_client_.count(buffer) == 0);

      in_use_by_client_.emplace(buffer, LinearOutput(buffer, this));
    }
    // ~guard

    // LinearOutput has taken ownership of the buffer.
    release_buffer.cancel();

    return std::make_pair(buffer, total_plane_size);
  }

  void Reset() override { output_buffer_pool_.Reset(true); }

  void StopAllWaits() override { output_buffer_pool_.StopAllWaits(); }

  gfx::Size GetRequiredSurfaceSize(const gfx::Size& picture_size) override {
    std::lock_guard<std::mutex> guard(surface_lock_);
    return GetRequiredSurfaceSizeLocked(picture_size);
  }

 protected:
  gfx::Size GetRequiredSurfaceSizeLocked(const gfx::Size& picture_size) FXL_REQUIRE(surface_lock_) {
    // Given the new picture size and the current surface size, create a surface size that will
    // allow us to hold decoded picture without shrinking the dimensions of the current DPB surface.
    // Since media-driver does not allow the surfaces to become smaller, ensure that the surface
    // dimensions are always at least equal to what they were before this function call.
    uint32_t unaligned_surface_width =
        safemath::checked_cast<uint32_t>(std::max(picture_size.width(), dpb_surface_size_.width()));
    uint32_t unaligned_surface_height = safemath::checked_cast<uint32_t>(
        std::max(picture_size.height(), dpb_surface_size_.height()));

    uint32_t aligned_surface_width = fbl::round_up(
        unaligned_surface_width, CodecAdapterVaApiDecoder::kLinearSurfaceWidthAlignment);
    uint32_t aligned_surface_height = fbl::round_up(
        unaligned_surface_height, CodecAdapterVaApiDecoder::kLinearSurfaceHeightAlignment);

    return {safemath::checked_cast<int>(aligned_surface_width),
            safemath::checked_cast<int>(aligned_surface_height)};
  }

  void OnSurfaceGenerationUpdatedLocked(size_t num_of_surfaces)
      FXL_REQUIRE(surface_lock_) override {
    // Clear all existing DPB surfaces that are not currently allocated to a reference frame. Any
    // surfaces that are currently being used as a reference frame will still be allocated for as
    // long as they are held by the decoder. Once they are no longer referenced they will be
    // destroyed instead of being returned back to |dpb_surfaces_|.
    dpb_surfaces_.clear();

    // Given the new picture size and the current surface size, create a surface size that will
    // allow us to hold decoded picture without shrinking the dimensions of the current DPB surface.
    // Since media-driver does not allow the surfaces to become smaller, ensure that the surface
    // dimensions are always at least equal to what they were before this function call.
    dpb_surface_size_ = GetRequiredSurfaceSizeLocked(coded_picture_size_);

    // Create the new number for requested DBP surfaces at the picture size.
    //
    // TODO(stefanbossbaly): Consider only replacing amount of unused surfaces in |dpb_surfaces_|
    // and then allocate the replacement surfaces once the old one is destroyed. This way reduce the
    // overall memory usage but more information gathering would need to be done to show that we
    // would not need to increase the number of DPB surfaces needed todo the decode operation, or we
    // can do sysmem incremental allocation and use it here.
    std::vector<VASurfaceID> va_surfaces(num_of_surfaces, VA_INVALID_SURFACE);
    VAStatus va_res =
        vaCreateSurfaces(VADisplayWrapper::GetSingleton()->display(), VA_RT_FORMAT_YUV420,
                         dpb_surface_size_.width(), dpb_surface_size_.height(), va_surfaces.data(),
                         static_cast<uint32_t>(va_surfaces.size()), nullptr, 0);

    if (va_res != VA_STATUS_SUCCESS) {
      std::ostringstream ss;
      ss << "vaCreateSurfaces failed: " << vaErrorStr(va_res);
      SetCodecFailure(ss.str());
      return;
    }

    for (VASurfaceID id : va_surfaces) {
      dpb_surfaces_.emplace_back(id);
    }
  }

 private:
  // VA-API outputs are distinct from the DPB and are stored in a regular
  // BufferPool, since the hardware doesn't necessarily support decoding to a
  // linear format like downstream consumers might need.
  class LinearOutput {
   public:
    LinearOutput() = default;
    LinearOutput(const CodecBuffer* buffer, LinearBufferManager* buffer_manager)
        : codec_buffer_(buffer), buffer_manager_(buffer_manager) {}
    ~LinearOutput() {
      if (buffer_manager_) {
        buffer_manager_->output_buffer_pool_.FreeBuffer(codec_buffer_->base());
      }
    }

    // Delete copying
    LinearOutput(const LinearOutput&) noexcept = delete;
    LinearOutput& operator=(const LinearOutput&) noexcept = delete;

    // Allow moving
    LinearOutput(LinearOutput&& other) noexcept {
      codec_buffer_ = other.codec_buffer_;
      buffer_manager_ = other.buffer_manager_;
      other.buffer_manager_ = nullptr;
    }

    LinearOutput& operator=(LinearOutput&& other) noexcept {
      codec_buffer_ = other.codec_buffer_;
      buffer_manager_ = other.buffer_manager_;
      other.buffer_manager_ = nullptr;
      return *this;
    }

   private:
    const CodecBuffer* codec_buffer_ = nullptr;
    LinearBufferManager* buffer_manager_ = nullptr;
  };

  // The order of output_buffer_pool_ and in_use_by_client_ matters, so that
  // destruction of in_use_by_client_ happens first, because those destructing
  // will return buffers to output_buffer_pool_.
  BufferPool output_buffer_pool_;
  std::map<const CodecBuffer*, LinearOutput> in_use_by_client_ FXL_GUARDED_BY(codec_lock_);

  // Holds the DPB surfaces that are allocated but not currently in use by the decoder. Once
  // GetDPBSurface() is called, the surface is then transferred from the ScopedSurfaceID RAII
  // wrapper to the a scoped_refptr<VASurface> wrapper and can not be destroyed until all references
  // of the wrapper are released.
  std::vector<ScopedSurfaceID> dpb_surfaces_ FXL_GUARDED_BY(surface_lock_) = {};
};

// This class manages output buffers when the client selects a tiled buffer output. Since the output
// is tiled the client will directly share the output from the decoded picture buffer (DPB). The
// manager will be responsible for creating the DPB surfaces that are backed by CodecBuffers the
// client provides us. The manager is also responsible for reconfiguring surfaces when a mid stream
// configuration change is required.
class TiledBufferManager : public SurfaceBufferManager {
 public:
  TiledBufferManager(std::mutex& codec_lock, CodecFailureCallback failure_callback)
      : SurfaceBufferManager(codec_lock, std::move(failure_callback)) {}
  ~TiledBufferManager() override = default;

  void AddBuffer(const CodecBuffer* buffer) override { output_buffer_pool_.AddBuffer(buffer); }

  void RecycleBuffer(const CodecBuffer* buffer) override {
    scoped_refptr<VASurface> to_drop;
    {
      std::lock_guard<std::mutex> guard(codec_lock_);
      ZX_DEBUG_ASSERT(in_use_by_client_.count(buffer) != 0);
      auto map_itr = in_use_by_client_.find(buffer);
      to_drop = std::move(map_itr->second);
      in_use_by_client_.erase(map_itr);
    }
    // ~ to_drop, which may trigger a buffer free callback if the decoder is no longer referencing
    // the frame
  }

  void DeconfigureBuffers() override {
    // Drop all references to buffers referenced by the client but keep the ones referenced by the
    // decoder
    {
      std::unordered_multimap<const CodecBuffer*, scoped_refptr<VASurface>> to_drop;
      {
        std::lock_guard<std::mutex> lock(codec_lock_);
        std::swap(to_drop, in_use_by_client_);
      }
    }
    // ~to_drop

    {
      std::lock_guard<std::mutex> guard(surface_lock_);
      allocated_free_surfaces_.clear();
    }

    ZX_DEBUG_ASSERT(!output_buffer_pool_.has_buffers_in_use());
  }

  // Getting a DPB requires that the surface is not in use by the client. This differs from the
  // linear version where DPB were not backed by a VMO. This function will block until a buffer is
  // recycled by the client or the manager is reset by the codec.
  scoped_refptr<VASurface> GetDPBSurface() override {
    const CodecBuffer* buffer = output_buffer_pool_.AllocateBuffer();

    if (!buffer) {
      return {};
    }

    // If any errors happen, release the buffer back into the pool
    auto release_buffer = fit::defer([&]() { output_buffer_pool_.FreeBuffer(buffer->base()); });

    std::lock_guard<std::mutex> guard(surface_lock_);
    VASurfaceID vmo_surface_id;

    // Check to see if there already is a surface allocated for this buffer
    auto map_itr = allocated_free_surfaces_.find(buffer);
    if (map_itr != allocated_free_surfaces_.end()) {
      vmo_surface_id = map_itr->second.release();
      allocated_free_surfaces_.erase(map_itr);
    } else {
      zx::vmo vmo_dup;
      zx_status_t zx_status = buffer->vmo().duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_dup);
      if (zx_status != ZX_OK) {
        FX_SLOG(WARNING, "Failed to duplicate vmo",
                KV("error_str", zx_status_get_string(zx_status)));
        return {};
      }

      const auto aligned_stride_checked = GetAlignedStride(dpb_surface_size_);
      const auto& [y_plane_checked, uv_plane_checked] = GetSurfacePlaneSizes(dpb_surface_size_);
      const auto pic_size_checked = (y_plane_checked + uv_plane_checked).Cast<uint32_t>();

      if (!aligned_stride_checked.IsValid()) {
        FX_SLOG(WARNING, "Aligned stride overflowed");
        return {};
      }

      if (!pic_size_checked.IsValid()) {
        FX_SLOG(WARNING, "Output picture size overflowed");
        return {};
      }

      size_t pic_size_bytes = static_cast<size_t>(pic_size_checked.ValueOrDie());
      ZX_ASSERT_MSG(buffer->size() >= pic_size_bytes,
                    "surface size (%zu bytes) exceeds buffer size (%zu bytes)", pic_size_bytes,
                    buffer->size());

      // For the moment we use DRM_PRIME_2 to represent VMOs.
      // To specify the destination VMO, we need two VASurfaceAttrib, one to set the
      // VASurfaceAttribMemoryType to VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2 and one for the
      // VADRMPRIMESurfaceDescriptor.
      VADRMPRIMESurfaceDescriptor ext_attrib{};
      VASurfaceAttrib attrib[2] = {
          {.type = VASurfaceAttribMemoryType,
           .flags = VA_SURFACE_ATTRIB_SETTABLE,
           .value = {.type = VAGenericValueTypeInteger,
                     .value = {.i = VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2}}},
          {.type = VASurfaceAttribExternalBufferDescriptor,
           .flags = VA_SURFACE_ATTRIB_SETTABLE,
           .value = {.type = VAGenericValueTypePointer, .value = {.p = &ext_attrib}}},
      };

      // VADRMPRIMESurfaceDescriptor
      ext_attrib.width = dpb_surface_size_.width();
      ext_attrib.height = dpb_surface_size_.height();
      ext_attrib.fourcc = VA_FOURCC_NV12;  // 2 plane YCbCr
      ext_attrib.num_objects = 1;
      ext_attrib.objects[0].fd = vmo_dup.release();
      ext_attrib.objects[0].drm_format_modifier =
          fuchsia::sysmem::FORMAT_MODIFIER_INTEL_I915_Y_TILED;
      ext_attrib.objects[0].size = pic_size_checked.ValueOrDie();
      ext_attrib.num_layers = 1;
      ext_attrib.layers[0].drm_format = make_fourcc('N', 'V', '1', '2');
      ext_attrib.layers[0].num_planes = 2;

      // Y plane
      ext_attrib.layers[0].object_index[0] = 0;
      ext_attrib.layers[0].pitch[0] = aligned_stride_checked.ValueOrDie();
      ext_attrib.layers[0].offset[0] = 0;

      // UV Plane
      ext_attrib.layers[0].object_index[1] = 0;
      ext_attrib.layers[0].pitch[1] = aligned_stride_checked.ValueOrDie();
      ext_attrib.layers[0].offset[1] = y_plane_checked.ValueOrDie();

      // Create one surface backed by the destination VMO.
      VAStatus status = vaCreateSurfaces(VADisplayWrapper::GetSingleton()->display(),
                                         VA_RT_FORMAT_YUV420, dpb_surface_size_.width(),
                                         dpb_surface_size_.height(), &vmo_surface_id, 1, attrib, 2);
      if (status != VA_STATUS_SUCCESS) {
        FX_SLOG(WARNING, "vaCreateSurfaces failed", KV("error_str", vaErrorStr(status)));
        return {};
      }
    }

    gfx::Size dpb_surface_size = dpb_surface_size_;
    uint64_t surface_generation = surface_generation_;

    // Callback that is called when the ref_count of this new constructed surface hits 0, This
    // occurs when the surface is no longer being used in the decoder (aka a new frame has replaced
    // us) and is no longer in use by the client (surface has been removed from in_use_by_client_).
    // Therefore once the VASurface release callback is called we can return this surface (and
    // therefore the VMO backing the surface) back into the pool of available surfaces.
    VASurface::ReleaseCB release_cb = [this, buffer, surface_generation](VASurfaceID surface_id) {
      {
        std::lock_guard<std::mutex> guard(surface_lock_);
        ZX_ASSERT(surface_to_buffer_.erase(surface_id) == 1);

        if (surface_generation_ == surface_generation) {
          allocated_free_surfaces_.emplace(buffer, surface_id);
        } else {
          auto status =
              vaDestroySurfaces(VADisplayWrapper::GetSingleton()->display(), &surface_id, 1);

          if (status != VA_STATUS_SUCCESS) {
            FX_SLOG(WARNING, "vaDestroySurfaces failed", KV("error_str", vaErrorStr(status)));
          }
        }
      }
      // ~guard

      output_buffer_pool_.FreeBuffer(buffer->base());
    };

    ZX_ASSERT(surface_to_buffer_.count(vmo_surface_id) == 0);
    surface_to_buffer_.emplace(vmo_surface_id, buffer);

    release_buffer.cancel();
    return std::make_shared<VASurface>(vmo_surface_id, dpb_surface_size, VA_RT_FORMAT_YUV420,
                                       std::move(release_cb));
  }

  std::optional<std::pair<const CodecBuffer*, uint32_t>> ProcessOutputSurface(
      scoped_refptr<VASurface> va_surface) override {
    VAStatus status = vaSyncSurface(VADisplayWrapper::GetSingleton()->display(), va_surface->id());
    if (status != VA_STATUS_SUCCESS) {
      FX_SLOG(ERROR, "SyncSurface failed", KV("error_str", vaErrorStr(status)));
      return std::nullopt;
    }

    const CodecBuffer* buffer = nullptr;

    {
      std::lock_guard<std::mutex> guard(surface_lock_);
      ZX_DEBUG_ASSERT(surface_to_buffer_.count(va_surface->id()) != 0);
      buffer = surface_to_buffer_[va_surface->id()];
    }

    if (!buffer) {
      return {};
    }

    const auto& [y_plane_checked, uv_plane_checked] = GetSurfacePlaneSizes(va_surface->size());
    const auto pic_size_checked = (y_plane_checked + uv_plane_checked).Cast<uint32_t>();
    if (!pic_size_checked.IsValid()) {
      FX_SLOG(WARNING, "Output picture size overflowed");
      return {};
    }

    // We are about to lend out the surface to the client so store the surface in in_use_by_client_
    // multimap so it increments the refcount until the client recycles it
    {
      std::lock_guard<std::mutex> guard(codec_lock_);
      in_use_by_client_.insert(std::make_pair(buffer, va_surface));
    }

    return std::make_pair(buffer, pic_size_checked.ValueOrDie());
  }

  void Reset() override { output_buffer_pool_.Reset(true); }

  void StopAllWaits() override { output_buffer_pool_.StopAllWaits(); }

  gfx::Size GetRequiredSurfaceSize(const gfx::Size& picture_size) override {
    std::lock_guard<std::mutex> guard(surface_lock_);
    return GetRequiredSurfaceSizeLocked(picture_size);
  }

 protected:
  gfx::Size GetRequiredSurfaceSizeLocked(const gfx::Size& picture_size) FXL_REQUIRE(surface_lock_) {
    // Given the new picture size and the current surface size, create a surface size that will
    // allow us to hold decoded picture without shrinking the dimensions of the current DPB surface.
    // Since media-driver does not allow the surfaces to become smaller, ensure that the surface
    // dimensions are always at least equal to what they were before this function call.
    uint32_t unaligned_surface_width =
        safemath::checked_cast<uint32_t>(std::max(picture_size.width(), dpb_surface_size_.width()));
    uint32_t unaligned_surface_height = safemath::checked_cast<uint32_t>(
        std::max(picture_size.height(), dpb_surface_size_.height()));

    uint32_t aligned_surface_width = fbl::round_up(
        unaligned_surface_width, CodecAdapterVaApiDecoder::kTileSurfaceWidthAlignment);
    uint32_t aligned_surface_height = fbl::round_up(
        unaligned_surface_height, CodecAdapterVaApiDecoder::kTileSurfaceHeightAlignment);

    return {safemath::checked_cast<int>(aligned_surface_width),
            safemath::checked_cast<int>(aligned_surface_height)};
  }

  void OnSurfaceGenerationUpdatedLocked(size_t num_of_surfaces)
      FXL_REQUIRE(surface_lock_) override {
    // This will call vaDestroySurface on all surfaces held by this data structure. Don't need to
    // reconstruct the surfaces here. They will be reconstructed once GetDPBSurface() is called and
    // the buffer has no linked surface.
    allocated_free_surfaces_.clear();

    // Given the new picture size and the current surface size, create a surface size that will
    // allow us to hold decoded picture without shrinking the dimensions of the current DPB surface.
    // Since media-driver does not allow the surfaces to become smaller, ensure that the surface
    // dimensions are always at least equal to what they were before this function call.
    dpb_surface_size_ = GetRequiredSurfaceSizeLocked(coded_picture_size_);
  }

 private:
  static safemath::internal::CheckedNumeric<uint32_t> GetAlignedStride(const gfx::Size& size) {
    auto aligned_stride = fbl::round_up(static_cast<uint64_t>(size.width()),
                                        CodecAdapterVaApiDecoder::kTileSurfaceWidthAlignment);
    return safemath::MakeCheckedNum(aligned_stride).Cast<uint32_t>();
  }

  static std::pair<safemath::internal::CheckedNumeric<uint32_t>,
                   safemath::internal::CheckedNumeric<uint32_t>>
  GetSurfacePlaneSizes(const gfx::Size& size) {
    // Depending on if the output is tiled or not we have to align our planes on tile boundaries
    // for both width and height
    auto aligned_stride = GetAlignedStride(size);
    auto aligned_y_height = static_cast<uint32_t>(size.height());
    auto aligned_uv_height = static_cast<uint32_t>(size.height()) / 2u;

    aligned_y_height =
        fbl::round_up(aligned_y_height, CodecAdapterVaApiDecoder::kTileSurfaceHeightAlignment);
    aligned_uv_height =
        fbl::round_up(aligned_uv_height, CodecAdapterVaApiDecoder::kTileSurfaceHeightAlignment);

    auto y_plane_size = safemath::CheckMul(aligned_stride, aligned_y_height);
    auto uv_plane_size = safemath::CheckMul(aligned_stride, aligned_uv_height);

    return std::make_pair(y_plane_size, uv_plane_size);
  }

  // Structure that maps allocated buffers shared with the client. Once the buffer is no longer in
  // use by the client and the decoder it should be removed from this map and marked as free in the
  // output_buffer_pool_.
  std::unordered_map<VASurfaceID, const CodecBuffer*> surface_to_buffer_
      FXL_GUARDED_BY(surface_lock_);

  // Once a surface is allocated it is stored in this map which maps the codec buffer that backs
  // the surface. If a resize event happens this structure will have to be invalidated and the
  // surfaces will have to be regenerated to match the new surface_size_
  std::unordered_map<const CodecBuffer*, ScopedSurfaceID> allocated_free_surfaces_
      FXL_GUARDED_BY(surface_lock_);

  // Maps the codec buffer to the VA surface being shared to the client. In addition to the
  // mapping this data structure holds a reference to the surface being used by the client,
  // preventing it from being destructed prior to it being recycled.
  // This has to be a multimap because it is possible to lend out the same surface concurrently to
  // the client and we don't want the destructor of the VASurface to be called when only one of the
  // lent out surfaces is recycled. For example on VP9 if show_existing_frame is marked true, we can
  // lend out the same surface concurrently.
  std::unordered_multimap<const CodecBuffer*, scoped_refptr<VASurface>> in_use_by_client_
      FXL_GUARDED_BY(codec_lock_);
};

void CodecAdapterVaApiDecoder::CoreCodecInit(
    const fuchsia::media::FormatDetails& initial_input_format_details) {
  if (!initial_input_format_details.has_format_details_version_ordinal()) {
    SetCodecFailure("CoreCodecInit(): Initial input format details missing version ordinal.");
    return;
  }
  // Will always be 0 for now.
  input_format_details_version_ordinal_ =
      initial_input_format_details.format_details_version_ordinal();

  const std::string& mime_type = initial_input_format_details.mime_type();
  if (mime_type == "video/h264-multi" || mime_type == "video/h264") {
    media_decoder_ = std::make_unique<media::H264Decoder>(std::make_unique<H264Accelerator>(this),
                                                          media::H264PROFILE_HIGH);
    is_h264_ = true;
  } else if (mime_type == "video/vp9") {
    media_decoder_ = std::make_unique<media::VP9Decoder>(std::make_unique<VP9Accelerator>(this),
                                                         media::VP9PROFILE_PROFILE0);
  } else {
    SetCodecFailure("CodecCodecInit(): Unknown mime_type %s\n", mime_type.c_str());
    return;
  }

  if (codec_diagnostics_) {
    std::string codec_name = is_h264_ ? "H264" : "VP9";
    codec_instance_diagnostics_ = codec_diagnostics_->CreateComponentCodec(codec_name);
  }

  VAConfigAttrib attribs[2] = {
      {.type = VAConfigAttribRTFormat, .value = VA_RT_FORMAT_YUV420},
      {.type = VAConfigAttribDecSliceMode, .value = VA_DEC_SLICE_MODE_NORMAL}};
  VAConfigID config_id;
  VAEntrypoint va_entrypoint = VAEntrypointVLD;
  VAStatus va_status;
  VAProfile va_profile;

  if (mime_type == "video/h264-multi" || mime_type == "video/h264") {
    va_profile = VAProfileH264High;
  } else if (mime_type == "video/vp9") {
    va_profile = VAProfileVP9Profile0;
  } else {
    SetCodecFailure("CodecCodecInit(): Unknown mime_type %s\n", mime_type.c_str());
    return;
  }

  va_status = vaCreateConfig(VADisplayWrapper::GetSingleton()->display(), va_profile, va_entrypoint,
                             attribs, std::size(attribs), &config_id);
  if (va_status != VA_STATUS_SUCCESS) {
    SetCodecFailure("CodecCodecInit(): Failed to create config: %s", vaErrorStr(va_status));
    return;
  }
  config_.emplace(config_id);

  int max_config_attributes = vaMaxNumConfigAttributes(VADisplayWrapper::GetSingleton()->display());
  std::vector<VAConfigAttrib> config_attributes(max_config_attributes);

  int num_config_attributes;
  va_status = vaQueryConfigAttributes(VADisplayWrapper::GetSingleton()->display(), config_->id(),
                                      &va_profile, &va_entrypoint, config_attributes.data(),
                                      &num_config_attributes);

  if (va_status != VA_STATUS_SUCCESS) {
    SetCodecFailure("CodecCodecInit(): Failed to query attributes: %s", vaErrorStr(va_status));
    return;
  }

  std::optional<uint32_t> max_height = std::nullopt;
  std::optional<uint32_t> max_width = std::nullopt;

  for (int i = 0; i < num_config_attributes; i += 1) {
    const VAConfigAttrib& attrib = config_attributes[i];
    switch (attrib.type) {
      case VAConfigAttribMaxPictureHeight:
        max_height = attrib.value;
        break;
      case VAConfigAttribMaxPictureWidth:
        max_width = attrib.value;
        break;
      default:
        break;
    }
  }

  if (!max_height) {
    FX_SLOG(WARNING, "Could not query hardware for max picture height supported. Setting default.");
  } else {
    max_picture_height_ = max_height.value();
  }

  if (!max_width) {
    FX_SLOG(WARNING, "Could not query hardware for max picture width supported. Setting default.");
  } else {
    max_picture_width_ = max_width.value();
  }

  zx_status_t result =
      input_processing_loop_.StartThread("input_processing_thread_", &input_processing_thread_);
  if (result != ZX_OK) {
    SetCodecFailure(
        "CodecCodecInit(): Failed to start input processing thread with "
        "zx_status_t: %d",
        result);
    return;
  }
}

void CodecAdapterVaApiDecoder::CoreCodecStartStream() {
  // It's ok for RecycleInputPacket to make a packet free anywhere in this
  // sequence. Nothing else ought to be happening during CoreCodecStartStream
  // (in this or any other thread).
  input_queue_.Reset();
  free_output_packets_.Reset(/*keep_data=*/true);

  {
    std::lock_guard<std::mutex> guard(lock_);
    is_stream_stopped_ = false;
  }

  // If the stream has initialized then reset
  if (surface_buffer_manager_) {
    surface_buffer_manager_->Reset();
  }

  LaunchInputProcessingLoop();

  TRACE_INSTANT("codec_runner", "Media:Start", TRACE_SCOPE_THREAD);
}

void CodecAdapterVaApiDecoder::CoreCodecResetStreamAfterCurrentFrame() {
  // Before we reset the decoder we must ensure that ProcessInputLoop() has exited and has no
  // outstanding tasks
  WaitForInputProcessingLoopToEnd();

  media_decoder_.reset();

  if (is_h264_) {
    media_decoder_ = std::make_unique<media::H264Decoder>(std::make_unique<H264Accelerator>(this),
                                                          media::H264PROFILE_HIGH);
  } else {
    media_decoder_ = std::make_unique<media::VP9Decoder>(std::make_unique<VP9Accelerator>(this),
                                                         media::VP9PROFILE_PROFILE0);
  }

  input_queue_.Reset(/*keep_data=*/true);

  LaunchInputProcessingLoop();
}

void CodecAdapterVaApiDecoder::DecodeAnnexBBuffer(media::DecoderBuffer buffer) {
  media_decoder_->SetStream(next_stream_id_++, buffer);

  while (true) {
    state_ = DecoderState::kDecoding;
    auto result = media_decoder_->Decode();
    state_ = DecoderState::kIdle;

    if (result == media::AcceleratedVideoDecoder::kConfigChange) {
      // We only need to request a output buffer reconfiguration if the current buffers are not able
      // to handle the new picture size. If they are able to handle the new picture size then the
      // new output format will be sent to the client and the current buffers will be kept. Since
      // linear and tiled format modifier effects how the planes are stored on the surface, the
      // surface manager is responsible for doing the calculation on how big the buffer needs to be
      // in order to store the output.
      auto output_re_config_required_result = IsBufferReconfigurationNeeded();

      if (output_re_config_required_result.is_error()) {
        SetCodecFailure(output_re_config_required_result.error_value().c_str());
        break;
      }

      ZX_ASSERT(output_re_config_required_result.is_ok());
      bool output_re_config_required = output_re_config_required_result.value();

      // If buffer reconfiguration is needed, reset mid_stream_output_buffer_reconfig_finish_ since
      // we are going to block the input_processing thread until either the stream is stopped or
      // CoreCodecMidStreamOutputBufferReConfigFinish() is called.
      if (output_re_config_required) {
        std::lock_guard<std::mutex> guard(lock_);
        mid_stream_output_buffer_reconfig_finish_ = false;
      }

      if (output_re_config_required) {
        // TODO(stefanbossbaly): Calling onCoreCodecMidStreamOutputConstraintsChange() with false is
        // now deprecated. Remove the |output_re_config_buffer| parameter.
        events_->onCoreCodecMidStreamOutputConstraintsChange(true);
      } else {
        // If an output reconfiguration was not needed, we still need to inform the client that the
        // output codec format has changed before the next output packet is sent to the client.
        events_->onCoreCodecOutputFormatChange();
      }

      gfx::Size pic_size = media_decoder_->GetPicSize();

      if (!context_id_) {
        // vaCreateContext's |picture_width| and |picture_height| parameters are only used to ensure
        // that they are not negative and do not exceed the maximum size allowed by the hardware.
        // Once vaRenderPicture() is called with a VADecPictureParameterBuffer struct as a
        // parameter, |picture_width| and |picture_height| parameters provided to vaCreateContext()
        // will be overridden.
        VAContextID context_id;
        VAStatus va_res = vaCreateContext(VADisplayWrapper::GetSingleton()->display(),
                                          config_->id(), pic_size.width(), pic_size.height(),
                                          VA_PROGRESSIVE, nullptr, 0, &context_id);
        if (va_res != VA_STATUS_SUCCESS) {
          SetCodecFailure("vaCreateContext failed: %s", vaErrorStr(va_res));
          break;
        }
        context_id_.emplace(context_id);
      }

      // Only wait if an output reconfiguration was required since sysmem will be required to add
      // the new buffers before preceding with the stream. Otherwise the buffers will be kept and
      // the new output constraints and format will be sent.
      if (output_re_config_required) {
        // Wait for the stream reconfiguration to finish before continuing to increment the
        // surface generation value
        std::unique_lock<std::mutex> lock(lock_);
        surface_buffer_manager_cv_.wait(lock, [this]() FXL_REQUIRE(lock_) {
          return mid_stream_output_buffer_reconfig_finish_ || is_stream_stopped_;
        });

        // If the stream is stopped, exit immediately
        if (is_stream_stopped_) {
          return;
        }
      }

      // Inform |surface_buffer_manager_| of what the current picture size. It is also possible for
      // the participants of sysmem to specify more than the min buffer count required by the
      // decoder, so use the buffer_count returned for the output buffer collection.
      ZX_ASSERT(buffer_counts_[kOutputPort]);
      ZX_DEBUG_ASSERT_MSG(
          buffer_counts_[kOutputPort].value() >= media_decoder_->GetRequiredNumOfPictures(),
          "buffer_count (%u) < Required Number of Pictures (%zu)",
          buffer_counts_[kOutputPort].value(), media_decoder_->GetRequiredNumOfPictures());
      surface_buffer_manager_->UpdatePictureSize(pic_size, buffer_counts_[kOutputPort].value());

      TRACE_INSTANT("codec_runner", "Configuration Change", TRACE_SCOPE_PROCESS, "pic_width",
                    TA_INT32(pic_size.width()), "pic_height", TA_INT32(pic_size.height()));

      continue;
    } else if (result == media::AcceleratedVideoDecoder::kRanOutOfStreamData) {
      // Reset decoder failures on successful decode
      decoder_failures_ = 0;
      break;
    } else {
      decoder_failures_ += 1;
      if (decoder_failures_ >= kMaxDecoderFailures) {
        SetCodecFailure(
            "Decoder exceeded the number of allowed failures. media_decoder::Decode result: "
            "%d",
            result);
      } else {
        // We allow the decoder to fail a set amount of times, reset the decoder after the current
        // frame. We need to stop the input_queue_ from processing any further items before the
        // stream reset. The stream control thread is responsible starting the stream once is has
        // been successfully reset.
        input_queue_.StopAllWaits();
        events_->onCoreCodecResetStreamAfterCurrentFrame();
      }

      break;
    }
  }
}  // ~buffer

const char* CodecAdapterVaApiDecoder::DecoderStateName(DecoderState state) {
  switch (state) {
    case DecoderState::kIdle:
      return "Idle";
    case DecoderState::kDecoding:
      return "Decoding";
    case DecoderState::kError:
      return "Error";
    default:
      return "UNKNOWN";
  }
}

template <class... Args>
void CodecAdapterVaApiDecoder::SetCodecFailure(const char* format, Args&&... args) {
  state_ = DecoderState::kError;
  events_->onCoreCodecFailCodec(format, std::forward<Args>(args)...);

  // Calling |onCoreCodecFailCodec()| will result in the closing of the StreamProcessor channel.
  // This task will be posted on the stream control thread, so it is possible that the channel will
  // not immediately be closed and therefore the call to |CoreCodecStopStream()| might have a slight
  // delay. The caller is expecting |SetCodecFailure()| to prevent further processing to be done
  // since it is possible the reason why the caller is failing is because it has detected an
  // unrecoverable error and wants to stops all video decoding processing. To handle this
  // gracefully, we will stop all waits on |input_queue_|. This will exit the |ProcessInputLoop()|
  // task which will cancel all pending and future operations. While it does not prevent the
  // enqueuing of new data, the call to |CoreCodecStopStream()| will happen in the near future,
  // which will clear out any operations that were enqueued in that time.
  input_queue_.StopAllWaits();
}

fitx::result<std::string, bool> CodecAdapterVaApiDecoder::IsBufferReconfigurationNeeded() const {
  // After issuing a kConfigChange, the media decoder picture size will now reflect what size
  // the current stream needs in order to proceed
  gfx::Size pic_size = media_decoder_->GetPicSize();
  gfx::Rect visible_rect = media_decoder_->GetVisibleRect();

  uint32_t coded_width = safemath::checked_cast<uint32_t>(pic_size.width());
  uint32_t coded_height = safemath::checked_cast<uint32_t>(pic_size.height());
  uint32_t display_width = safemath::checked_cast<uint32_t>(visible_rect.width());
  uint32_t display_height = safemath::checked_cast<uint32_t>(visible_rect.height());

  // Ensure that the new picture size is within the allowed hardware requirements
  if (coded_height > max_picture_height_) {
    FX_SLOG(ERROR, "coded_height exceeds max_picture_height_", KV("coded_height", coded_height),
            KV("max_picture_height_", max_picture_height_));
    std::ostringstream oss;
    oss << "Requested picture height " << coded_height
        << " exceeds max hardware supported height of " << max_picture_height_;
    return fitx::error(oss.str());
  }
  if (coded_width > max_picture_width_) {
    FX_SLOG(ERROR, "coded_width exceeds max_picture_width_", KV("coded_width", coded_width),
            KV("max_picture_width_", max_picture_width_));
    std::ostringstream oss;
    oss << "Requested picture width " << coded_width << " exceeds max hardware supported width of "
        << max_picture_width_;
    return fitx::error(oss.str());
  }

  // Ensure that we have buffers already configured, if not then a reconfiguration is always needed
  if (!surface_buffer_manager_ || !buffer_settings_[kOutputPort].has_value()) {
    return fitx::ok(true);
  }

  ZX_ASSERT(buffer_settings_[kOutputPort]->has_image_format_constraints);
  auto surface_size = surface_buffer_manager_->GetRequiredSurfaceSize(pic_size);

  // TODO(stefanbossbaly): This isn't the correct calculation as it does not factor in alignment
  // for tiled surfaces
  auto total_plane_size_checked =
      ((safemath::MakeCheckedNum(surface_size.GetArea()) * 3) / 2).Cast<uint32_t>();

  // The check above should ensure that we never get to an unsupported hardware size, but
  // better safe than sorry when calling ValueOrDie()
  if (!total_plane_size_checked.IsValid()) {
    FX_SLOG(ERROR, "Surface size exceeds the max hardware supported size");
    return fitx::error("Surface size exceeds the max hardware supported size");
  }
  uint32_t total_plane_size = total_plane_size_checked.ValueOrDie();

  // Ensure the size of the buffers can hold the new plane size
  if (total_plane_size > buffer_settings_[kOutputPort]->buffer_settings.size_bytes) {
    FX_SLOG(DEBUG, "total_plane_size > buffer_size_bytes", KV("total_plane_size", total_plane_size),
            KV("buffer_size_bytes", buffer_settings_[kOutputPort]->buffer_settings.size_bytes));
    return fitx::ok(true);
  }

  const auto& image_constraints = buffer_settings_[kOutputPort]->image_format_constraints;

  if (display_width % image_constraints.display_width_divisor != 0u) {
    FX_SLOG(DEBUG, "display_width not divisible by display_width_divisor",
            KV("display_width", display_width),
            KV("display_width_divisor", image_constraints.display_width_divisor));
    // These will fail, but let them fail when trying to re-negotiate sysmem buffers.
    return fitx::ok(true);
  }

  if (display_height % image_constraints.display_height_divisor != 0u) {
    FX_SLOG(DEBUG, "display_height not divisible by display_height_divisor",
            KV("display_height", display_height),
            KV("display_height_divisor", image_constraints.display_height_divisor));
    // These will fail, but let them fail when trying to re-negotiate sysmem buffers.
    return fitx::ok(true);
  }

  auto coded_area_checked = safemath::CheckMul(coded_width, coded_height).Cast<uint32_t>();
  if (!coded_area_checked.IsValid()) {
    FX_SLOG(ERROR, "Surface size exceeds uint32_t", KV("coded_width", coded_width),
            KV("coded_height", coded_height));
    return fitx::error("Surface size exceeds uint32_t");
  }
  uint32_t coded_area = coded_area_checked.ValueOrDie();
  if (coded_area > image_constraints.max_coded_width_times_coded_height) {
    FX_SLOG(DEBUG, "coded_area > max_coded_width_times_coded_height", KV("coded_area", coded_area),
            KV("max_coded_width_times_coded_height",
               image_constraints.max_coded_width_times_coded_height));
    // These will very likely fail, but let them fail when trying to re-negotiate sysmem buffers.
    return fitx::ok(true);
  }

  if (coded_width % image_constraints.coded_width_divisor != 0u) {
    FX_SLOG(DEBUG, "coded_width not divisible by coded_width_divisor",
            KV("coded_width", coded_width),
            KV("coded_width_divisor", image_constraints.coded_width_divisor));
    // These will fail, but let them fail when trying to re-negotiate sysmem buffers.
    return fitx::ok(true);
  }

  if (coded_height % image_constraints.coded_height_divisor != 0u) {
    FX_SLOG(DEBUG, "coded_height not divisible by coded_height_divisor",
            KV("coded_height", coded_height),
            KV("coded_height_divisor", image_constraints.coded_height_divisor));
    // These will fail, but let them fail when trying to re-negotiate sysmem buffers.
    return fitx::ok(true);
  }

  if (coded_width < image_constraints.min_coded_width) {
    FX_SLOG(DEBUG, "coded_width < min_coded_width", KV("coded_width", coded_width),
            KV("min_coded_width", image_constraints.min_coded_width));
    return fitx::ok(true);
  }

  if (coded_width > image_constraints.max_coded_width) {
    FX_SLOG(DEBUG, "coded_width > max_coded_width", KV("coded_width", coded_width),
            KV("max_coded_width", image_constraints.max_coded_width));
    return fitx::ok(true);
  }

  if (coded_height < image_constraints.min_coded_height) {
    FX_SLOG(DEBUG, "coded_height < min_coded_height", KV("coded_height", coded_height),
            KV("min_coded_height", image_constraints.min_coded_height));
    return fitx::ok(true);
  }

  if (coded_height > image_constraints.max_coded_height) {
    FX_SLOG(DEBUG, "coded_height > max_coded_height", KV("coded_height", coded_height),
            KV("max_coded_height", image_constraints.max_coded_height));
    return fitx::ok(true);
  }

  uint32_t stride = safemath::checked_cast<uint32_t>(surface_size.width());
  if (stride < image_constraints.min_bytes_per_row) {
    FX_SLOG(DEBUG, "stride < min_bytes_per_row", KV("stride", stride),
            KV("min_bytes_per_row", image_constraints.min_bytes_per_row));
    return fitx::ok(true);
  }

  if (stride > image_constraints.max_bytes_per_row) {
    FX_SLOG(DEBUG, "stride > max_bytes_per_row", KV("stride", stride),
            KV("max_bytes_per_row", image_constraints.max_bytes_per_row));
    return fitx::ok(true);
  }

  // This check only makes sense if the output is linear since tiled formats don't really have a
  // concept of bytes per row divisor
  if (!IsOutputTiled()) {
    if (stride % image_constraints.bytes_per_row_divisor != 0u) {
      FX_SLOG(DEBUG, "stride not divisible by bytes_per_row_divisor", KV("stride", stride),
              KV("bytes_per_row_divisor", image_constraints.bytes_per_row_divisor));
      // These will fail, but let them fail when trying to re-negotiate sysmem buffers.
      return fitx::ok(true);
    }
  }

  // The current buffers meet all conditions, output reconfiguration is not needed
  return fitx::ok(false);
}

void CodecAdapterVaApiDecoder::ProcessInputLoop() {
  std::optional<CodecInputItem> maybe_input_item;
  while ((maybe_input_item = input_queue_.WaitForElement())) {
    CodecInputItem input_item = std::move(maybe_input_item.value());
    if (input_item.is_format_details()) {
      const std::string& mime_type = input_item.format_details().mime_type();

      if ((!is_h264_ && (mime_type == "video/h264-multi" || mime_type == "video/h264")) ||
          (is_h264_ && mime_type == "video/vp9")) {
        SetCodecFailure(
            "CodecCodecInit(): Can not switch codec type after setting it in CoreCodecInit(). "
            "Attempting to switch it to %s\n",
            mime_type.c_str());
        return;
      }

      if (mime_type == "video/h264-multi" || mime_type == "video/h264") {
        avcc_processor_.ProcessOobBytes(input_item.format_details());
      }
    } else if (input_item.is_end_of_stream()) {
      // TODO(stefanbossbaly): Encapsulate in abstraction
      if (is_h264_) {
        constexpr uint8_t kEndOfStreamNalUnitType = 11;
        // Force frames to be processed.
        std::vector<uint8_t> end_of_stream_delimiter{0, 0, 1, kEndOfStreamNalUnitType};

        media::DecoderBuffer buffer(end_of_stream_delimiter);
        media_decoder_->SetStream(next_stream_id_++, buffer);
        state_ = DecoderState::kDecoding;
        auto result = media_decoder_->Decode();
        state_ = DecoderState::kIdle;
        if (result != media::AcceleratedVideoDecoder::kRanOutOfStreamData) {
          SetCodecFailure("Unexpected media_decoder::Decode result for end of stream: %d", result);
          return;
        }
      }

      bool res = media_decoder_->Flush();
      if (!res) {
        FX_SLOG(WARNING, "media decoder flush failed");
      }
      events_->onCoreCodecOutputEndOfStream(/*error_detected_before=*/!res);
    } else if (input_item.is_packet()) {
      auto* packet = input_item.packet();
      ZX_DEBUG_ASSERT(packet->has_start_offset());
      if (packet->has_timestamp_ish()) {
        stream_to_pts_map_.emplace_back(next_stream_id_, packet->timestamp_ish());
        constexpr size_t kMaxPtsMapSize = 64;
        if (stream_to_pts_map_.size() > kMaxPtsMapSize)
          stream_to_pts_map_.pop_front();
      }

      const uint8_t* buffer_start = packet->buffer()->base() + packet->start_offset();
      size_t buffer_size = packet->valid_length_bytes();

      bool returned_buffer = false;
      auto return_input_packet =
          fit::defer_callback(fit::closure([this, &input_item, &returned_buffer] {
            events_->onCoreCodecInputPacketDone(input_item.packet());
            returned_buffer = true;
          }));

      if (is_h264_ && avcc_processor_.is_avcc()) {
        // TODO(fxbug.dev/94139): Remove this copy.
        auto output_avcc_vec = avcc_processor_.ParseVideoAvcc(buffer_start, buffer_size);
        media::DecoderBuffer buffer(output_avcc_vec, packet->buffer(), packet->start_offset(),
                                    std::move(return_input_packet));
        DecodeAnnexBBuffer(std::move(buffer));
      } else {
        media::DecoderBuffer buffer({buffer_start, buffer_size}, packet->buffer(),
                                    packet->start_offset(), std::move(return_input_packet));
        DecodeAnnexBBuffer(std::move(buffer));
      }

      // Ensure that the decode buffer has been destroyed and the input packet has been returned
      ZX_ASSERT(returned_buffer);

      // TODO(stefanbossbaly): Encapsulate in abstraction
      if (is_h264_) {
        constexpr uint8_t kAccessUnitDelimiterNalUnitType = 9;
        constexpr uint8_t kPrimaryPicType = 1 << (7 - 3);
        // Force frames to be processed. TODO(jbauman): Key on known_end_access_unit.
        std::vector<uint8_t> access_unit_delimiter{0, 0, 1, kAccessUnitDelimiterNalUnitType,
                                                   kPrimaryPicType};

        media::DecoderBuffer buffer(access_unit_delimiter);
        media_decoder_->SetStream(next_stream_id_++, buffer);
        state_ = DecoderState::kDecoding;
        auto result = media_decoder_->Decode();
        state_ = DecoderState::kIdle;
        if (result != media::AcceleratedVideoDecoder::kRanOutOfStreamData) {
          SetCodecFailure("Unexpected media_decoder::Decode result for delimiter: %d", result);
          return;
        }
      }
    }
  }
}

void CodecAdapterVaApiDecoder::CleanUpAfterStream() {
  {
    // TODO(stefanbossbaly): Encapsulate in abstraction
    if (is_h264_) {
      // Force frames to be processed.
      std::vector<uint8_t> end_of_stream_delimiter{0, 0, 1, 11};

      media::DecoderBuffer buffer(end_of_stream_delimiter);
      media_decoder_->SetStream(next_stream_id_++, buffer);
      auto result = media_decoder_->Decode();
      if (result != media::AcceleratedVideoDecoder::kRanOutOfStreamData) {
        SetCodecFailure("Unexpected media_decoder::Decode result for end of stream: %d", result);
        return;
      }
    }
  }

  bool res = media_decoder_->Flush();
  if (!res) {
    FX_SLOG(WARNING, "media decoder flush failed");
  }
}

void CodecAdapterVaApiDecoder::CoreCodecMidStreamOutputBufferReConfigFinish() {
  // Currently once the surface_buffer_manager_ has been constructed, it can not be destructed until
  // the end of the stream. This means once the client have chosen a format modifier, it can not be
  // changed
  if (!surface_buffer_manager_) {
    auto failure_callback = SurfaceBufferManager::CodecFailureCallback(
        [this](const std::string& failure_message) { SetCodecFailure(failure_message.c_str()); });

    if (IsOutputTiled()) {
      surface_buffer_manager_ =
          std::make_unique<TiledBufferManager>(lock_, std::move(failure_callback));
    } else {
      surface_buffer_manager_ =
          std::make_unique<LinearBufferManager>(lock_, std::move(failure_callback));
    }
  }

  LoadStagedOutputBuffers();

  // Signal that we are done with the mid stream output buffer configuration to other threads
  {
    std::lock_guard<std::mutex> guard(lock_);
    mid_stream_output_buffer_reconfig_finish_ = true;
    surface_buffer_manager_cv_.notify_all();
  }
}

bool CodecAdapterVaApiDecoder::ProcessOutput(scoped_refptr<VASurface> va_surface,
                                             int bitstream_id) {
  auto maybe_processed_surface =
      surface_buffer_manager_->ProcessOutputSurface(std::move(va_surface));

  if (!maybe_processed_surface) {
    return true;
  }

  auto& [codec_buffer, pic_size_bytes] = maybe_processed_surface.value();

  auto release_buffer = fit::defer([this, codec_buffer = codec_buffer]() {
    surface_buffer_manager_->RecycleBuffer(codec_buffer);
  });

  std::optional<CodecPacket*> maybe_output_packet = free_output_packets_.WaitForElement();
  if (!maybe_output_packet) {
    // Wait will succeed unless we're dropping all remaining frames of a stream.
    return true;
  }

  auto output_packet = maybe_output_packet.value();
  output_packet->SetBuffer(codec_buffer);
  output_packet->SetStartOffset(0);
  output_packet->SetValidLengthBytes(pic_size_bytes);
  {
    auto pts_it =
        std::find_if(stream_to_pts_map_.begin(), stream_to_pts_map_.end(),
                     [bitstream_id](const auto& pair) { return pair.first == bitstream_id; });
    if (pts_it != stream_to_pts_map_.end()) {
      output_packet->SetTimstampIsh(pts_it->second);
    } else {
      output_packet->ClearTimestampIsh();
    }
  }

  release_buffer.cancel();
  events_->onCoreCodecOutputPacket(output_packet,
                                   /*error_detected_before=*/false,
                                   /*error_detected_during=*/false);
  return true;
}

scoped_refptr<VASurface> CodecAdapterVaApiDecoder::GetVASurface() {
  return surface_buffer_manager_->GetDPBSurface();
}
