// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_ISP_MALI_009_STREAM_SERVER_H_
#define SRC_CAMERA_DRIVERS_ISP_MALI_009_STREAM_SERVER_H_

#include <fuchsia/sysmem/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <zircon/types.h>

#include <list>
#include <unordered_set>

#include "stream_impl.h"

namespace camera {

// |StreamImpl| provides a simple stream interface usable by multiple consumers. It is intended to
// be used only in conjunction with ArmIspDeviceTester.
// On creation, the class creates a set of buffers to be used by the ISP stream, and starts a
// message loop to handle client messages. Clients are added via the AddClient method.
// Start, Stop, and ReleaseFrame are not implemented as the class, not its clients, controls
// the function of the stream. The OnFrameAvailable event is forwarded to clients.
class StreamServer {
 public:
  StreamServer() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

  // Create a server and return writable buffer handles.
  static zx_status_t Create(zx::bti* bti, std::unique_ptr<StreamServer>* server_out,
                            fuchsia_sysmem_BufferCollectionInfo_2* buffers_out,
                            fuchsia_sysmem_ImageFormat_2* format_out);

  // Add a client and return read-only buffer handles.
  zx_status_t AddClient(zx::channel channel, fuchsia_sysmem_BufferCollectionInfo_2* buffers_out);

  // Called when a new frame is available from the ISP.
  void FrameAvailable(uint32_t id, std::list<uint32_t>* out_frames_to_be_released);

  // Gets the number of clients connected to the server.
  size_t GetNumClients() { return streams_.size(); }

 private:
  zx_status_t GetBuffers(fuchsia_sysmem_BufferCollectionInfo_2* buffers_out);

  uint32_t next_stream_id_ = 1;
  std::map<uint32_t, std::unique_ptr<camera::StreamImpl>> streams_;
  async::Loop loop_;
  fuchsia::sysmem::BufferCollectionInfo_2 buffers_;
  std::unordered_set<uint32_t> read_locked_buffers_;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_ISP_MALI_009_STREAM_SERVER_H_
