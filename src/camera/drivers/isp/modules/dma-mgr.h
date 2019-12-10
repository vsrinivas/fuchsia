// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_ISP_MODULES_DMA_MGR_H_
#define SRC_CAMERA_DRIVERS_ISP_MODULES_DMA_MGR_H_

#include <fuchsia/camera/c/fidl.h>
#include <fuchsia/sysmem/c/fidl.h>
#include <lib/fit/function.h>
#include <lib/fzl/vmo-pool.h>

#include <deque>

#include "../mali-009/pingpong_regs.h"
#include "dma-format.h"

namespace camera {

class DmaManager {
 public:
  enum class Stream : bool { FullResolution = false, Downscaled };

  DmaManager(Stream stream_type, const ddk::MmioView& isp_mmio_local)
      : isp_mmio_local_(isp_mmio_local), stream_type_(stream_type) {}

  // Initialize the format and buffers of the DMA Writer.
  // |buffer_collection| contains the vmos to which the DMA will write.
  // |image_format| provides the image format information that dictates
  // the DMA's configuration.
  // |frame_available_callback| will be called when the DMA is done writing
  // to a buffer.
  // A note on making multiple Configure() calls:
  // It is possible to transition the DMA manager to another format by calling
  // Configure() with a different buffer collection.  However, doing so will
  // remove all knowledge of the locked status of frames of the previous
  // BufferCollection.
  // This has the following effects:
  //  - Frames that are currently being written will be dropped.  Calls to
  //  On*FrameWritten()
  //    will only relate to frames written in the new BufferCollection.
  //    TODO(CAM-54): Provide a way to dump the previous set of write locked
  //    buffers.
  //  - ReleaseFrame calls with currently used indices (relating to the old
  //  BufferCollection)
  //    will return errors.
  zx_status_t Configure(
      fuchsia_sysmem_BufferCollectionInfo_2 buffer_collection,
      const fuchsia_sysmem_ImageFormat_2& image_format,
      fit::function<void(fuchsia_camera_FrameAvailableEvent)> frame_available_callback);

  static zx_status_t Create(const zx::bti& bti, const ddk::MmioView& isp_mmio_local,
                            Stream stream_type, std::unique_ptr<DmaManager>* out);

  // Loads a frame and writes the frame info into the current register block.
  // Updates the dma writer with the address of a free buffer from the pool.
  // This is called when initializing multiple buffers in preparation for
  // enabling streaming.
  void LoadNewFrame();

  // Called when the new_frame interrupt is triggered.  Processes any previously
  // written frames, including calling the frame_available_callback and loads a
  // new frame.
  void OnNewFrame();

  // Signal that all consumers are done with this frame.
  zx_status_t ReleaseFrame(uint32_t buffer_index);

  // Prints status registers. Used for debugging.
  void PrintStatus(ddk::MmioBuffer* mmio);

  // Allow the streaming of frames to a consumer.
  void Enable();

  // Stop writing frames and sending them to the consumer.
  // If frames are currently being written, they will be dropped.
  void Disable();

  bool enabled() { return enabled_; }

 private:
  bool enabled_ = false;
  bool first_frame_ = true;
  ddk::MmioView isp_mmio_local_;
  fzl::VmoPool buffers_;
  std::deque<std::optional<fzl::VmoPool::Buffer>> write_locked_buffers_;
  std::optional<DmaFormat> current_format_;
  Stream stream_type_;
  fit::function<void(fuchsia_camera_FrameAvailableEvent)> frame_available_callback_;
  zx::bti bti_;
  uint64_t buffer_underrun_sequential_count_ = 0;

  // Releases the write lock on the frame and calls the
  // frame_available_callback.
  void OnFrameWritten();

  // Get the Registers used by the DMA Writer.
  auto GetPrimaryMisc();
  auto GetUvMisc();
  auto GetPrimaryBank0();
  auto GetUvBank0();
  auto GetPrimaryActiveDim();
  auto GetUvActiveDim();
  auto GetPrimaryLineOffset();
  auto GetUvLineOffset();
  auto GetPrimaryFrameCount();
  auto GetUvFrameCount();
  auto GetPrimaryFailures();
  auto GetUvFailures();

  // Writes the dma format to the registers
  void WriteFormat();
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_ISP_MODULES_DMA_MGR_H_
