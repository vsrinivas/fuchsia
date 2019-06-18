// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_ISP_MODULES_DMA_MGR_H_
#define SRC_CAMERA_DRIVERS_ISP_MODULES_DMA_MGR_H_

#include <fuchsia/camera/common/c/fidl.h>
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

  DmaManager(Stream stream_type, ddk::MmioView isp_mmio_local)
      : isp_mmio_local_(isp_mmio_local), stream_type_(stream_type) {}

  // Initialize the format and buffers of the DMA Writer.  Calling this
  // function enables the DMA's streaming functions.
  // |buffer_collection| contains the vmos that the DMA will write to, and
  // the image format that dictates the DMA's configuration.
  // |frame_available_callback| will be called when the DMA is done writing
  // to a buffer.
  // A note on making multiple Start() calls:
  // The way to transition the DMA manager to another format is to call Start()
  // with a different buffer collection.  However, doing so will remove all
  // knowledge of the locked status of frames of the previous BufferCollection.
  // This has the following effects:
  //  - Frames that are currently being written will be dropped.  Calls to
  //  On*FrameWritten()
  //    will only relate to frames written in the new BufferCollection.
  //    TODO(CAM-54): Provide a way to dump the previous set of write locked
  //    buffers.
  //  - ReleaseFrame calls with currently used indices (relating to the old
  //  BufferCollection)
  //    will return errors.
  zx_status_t Start(
      fuchsia_sysmem_BufferCollectionInfo buffer_collection,
      fit::function<void(fuchsia_camera_common_FrameAvailableEvent)>
          frame_available_callback);

  static zx_status_t Create(const zx::bti& bti, ddk::MmioView isp_mmio_local,
                            Stream stream_type,
                            std::unique_ptr<DmaManager>* out);

  // Updates the dma writer with the address of a free buffer from the pool.
  void OnNewFrame();

  // This should be called when the appropriate "y-DMA write done" interrupt
  // is triggered.
  void OnPrimaryFrameWritten();
  // This should be called when the appropriate "uv-DMA write done" interrupt
  // is triggered (if a secondary channel is used).
  void OnSecondaryFrameWritten();

  // Signal that all consumers are done with this frame.
  zx_status_t ReleaseFrame(uint32_t buffer_index);

 private:
  bool enabled_ = false;
  ddk::MmioView isp_mmio_local_;
  fzl::VmoPool buffers_;
  std::deque<fzl::VmoPool::Buffer> write_locked_buffers_;
  std::optional<DmaFormat> current_format_;
  Stream stream_type_;
  fit::function<void(fuchsia_camera_common_FrameAvailableEvent)>
      frame_available_callback_;
  zx::bti bti_;
  bool primary_frame_written_ = false, secondary_frame_written_ = false;

  // Get the Registers used by the DMA Writer.
  auto GetPrimaryMisc();
  auto GetUvMisc();
  auto GetPrimaryBank0();
  auto GetUvBank0();
  auto GetPrimaryActiveDim();
  auto GetUvActiveDim();
  auto GetPrimaryLineOffset();
  auto GetUvLineOffset();

  // Writes the dma format to the registers
  void WriteFormat();
  // Releases the write lock on the frame and calls the
  // frame_available_callback.
  void OnFrameWritten();
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_ISP_MODULES_DMA_MGR_H_
