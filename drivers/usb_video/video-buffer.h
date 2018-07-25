// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_USB_VIDEO_VIDEO_BUFFER_H_
#define GARNET_DRIVERS_USB_VIDEO_VIDEO_BUFFER_H_

#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <lib/zx/vmo.h>
#include <zircon/types.h>

namespace video {
namespace usb {

// This class is not thread safe.
class VideoBuffer {
 public:
  // The position (in bytes) of the start of the frame in the video buffer.
  typedef uint64_t FrameOffset;

  // Creates a VideoBuffer with the given VMO buffer handle.
  // If successful, returns ZX_OK and a pointer to the created
  // VideoBuffer will be stored in out.
  static zx_status_t Create(zx::vmo&& vmo, fbl::unique_ptr<VideoBuffer>* out,
                            uint32_t max_frame_size);

  // Initializes the video buffer for a new streaming session.
  void Init();

  // Finds the next available frame for the driver to write to, and sets
  // the frame as currently in progress.
  // Returns ZX_OK if successful, and stores the frame offset into out_offset.
  // Returns ZX_ERR_NOT_FOUND if no frames were available or ZX_ERR_BAD_STATE
  // if a frame is in the currently in progress state.
  zx_status_t GetNewFrame(FrameOffset* out_offset);

  // Sets the currently in progress frame as completed and ready to consume.
  // The frame will be locked until FrameRelease is called with its offset.
  // Returns ZX_OK if successful, or ZX_ERR_NOT_FOUND if no frame is
  // currently in progress.
  zx_status_t FrameCompleted();

  // Unlocks the frame with the specified offset and sets it as ready to be
  // reused.
  // Returns ZX_OK if successful, or ZX_ERR_NOT_FOUND if no locked frame
  // was found with the given offset.
  zx_status_t FrameRelease(FrameOffset offset);

  uint64_t size() const { return size_; }
  void* virt() const { return virt_; }

  ~VideoBuffer();

 private:
  VideoBuffer(zx::vmo&& vmo, uint64_t size, void* virt)
      : vmo_(fbl::move(vmo)), size_(size), virt_(virt) {}

  // Allocates the free_frames_ and locked_frames_ vectors.
  zx_status_t Alloc(uint32_t max_frame_size);

  // VMO backing the video buffer.
  zx::vmo vmo_;
  // Size of the VMO.
  uint64_t size_ = 0;
  // The mapped address of the start of the video buffer.
  void* virt_ = nullptr;

  // Frames that are available for writing to.
  fbl::Vector<FrameOffset> free_frames_;
  // Frames that have been locked for the client and should not be overwritten.
  fbl::Vector<FrameOffset> locked_frames_;

  // Whether the driver is currently writing to the in_progress_frame_.
  bool has_in_progress_frame_ = false;
  // The frame that is currently being written to.
  // Only valid if in_progress_frame_valid_ is true.
  FrameOffset in_progress_frame_;
};

}  // namespace usb
}  // namespace video

#endif  // GARNET_DRIVERS_USB_VIDEO_VIDEO_BUFFER_H_
