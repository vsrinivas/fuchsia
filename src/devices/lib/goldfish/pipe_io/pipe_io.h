// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_GOLDFISH_PIPE_IO_PIPE_IO_H_
#define SRC_DEVICES_LIB_GOLDFISH_PIPE_IO_PIPE_IO_H_

#include <fuchsia/hardware/goldfish/pipe/cpp/banjo.h>
#include <lib/ddk/device.h>
#include <lib/ddk/io-buffer.h>
#include <lib/fit/function.h>
#include <lib/fit/result.h>
#include <lib/stdcompat/span.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/status.h>

#include <map>
#include <vector>

#include <ddktl/device.h>
#include <fbl/mutex.h>

namespace goldfish {

// PipeIo is a generic library drivers could use to read from and write to
// goldfish Pipe devices. It supports blocking and non-blocking read / write
// and read / write with frame headers, which are used in some QEMUD pipes
// like goldfish sensor pipe.
class PipeIo {
 public:
  PipeIo(const ddk::GoldfishPipeProtocolClient* pipe, const char* pipe_name);
  ~PipeIo();

  using ReadResult = fit::result<std::vector<uint8_t>, zx_status_t>;

  // Read |size| bytes from the pipe.
  // Returns:
  // - |ZX_OK|: if read succeeds.
  // - |ZX_ERR_SHOULD_WAIT|: only occurs when |blocking| = false, when fewer
  //          than |size| bytes can be read from the pipe without waiting.
  // - Other errors: if pipe read fails, or event wait fails.
  ReadResult Read(size_t size, bool blocking = false);

  // First read the header for payload size, then read the payload.
  // Returns:
  // - |ZX_OK|: if read succeeds.
  // - |ZX_ERR_SHOULD_WAIT|: only occurs when |blocking| = false, when less than
  //          4 bytes cannot be read (for header), or fewer than |size| bytes
  //          can be read from the pipe without waiting.
  //   Other errors: if pipe read fails, or event wait fails.
  ReadResult ReadWithHeader(bool blocking = false);

  // Write a null-terminated string |payload| to the pipe. The terminator '\0'
  // will be sent to the pipe as well.
  // Returns:
  // - |ZX_OK|: if write succeeds.
  // - |ZX_ERR_SHOULD_WAIT|: only occurs when |blocking| = false, when fewer
  //          than the payload size can be written to the pipe without waiting.
  //   Other errors: if pipe write fails, or event wait fails.
  zx_status_t Write(const char* payload, bool blocking = false);

  // Write a byte vector |payload| to the pipe. The terminator '\0'
  // will be sent to the pipe as well.
  // Returns: Same as "Write(const char*, bool)".
  zx_status_t Write(const std::vector<uint8_t>& payload, bool blocking = false);

  // First write the header of the payload (4-bytes size in hexadecimal), then
  // write the payload.
  // Returns:
  // - |ZX_OK|: if write succeeds.
  // - |ZX_ERR_SHOULD_WAIT|: only occurs when |blocking| = false, when less than
  //          4 bytes cannot be written (for header), or fewer than |size| bytes
  //          can be written to the pipe without waiting.
  //   Other errors: if pipe read fails, or event wait fails.
  zx_status_t WriteWithHeader(const char* payload, bool blocking = false);

  // First write the header of the payload (4-bytes size), then write the
  // payload.
  // Returns: Same as "WriteWithHeader(const char*, bool)".
  zx_status_t WriteWithHeader(const std::vector<uint8_t>& payload, bool blocking = false);

  const zx::event& pipe_event() const { return pipe_event_; }
  bool valid() const { return valid_; }

 private:
  zx_status_t Init(const char* pipe_name);
  zx_status_t SetupPipe();
  zx_status_t ReadOnceLocked(std::vector<uint8_t>& buf, size_t size) TA_REQ(lock_);
  zx_status_t TransferLocked(int32_t cmd, int32_t wake_cmd, zx_signals_t state_clr,
                             uint32_t write_bytes, uint32_t read_bytes, uint32_t* actual)
      TA_REQ(lock_);

  fbl::Mutex lock_;

  bool valid_ = false;
  int32_t id_ = 0;
  zx::bti bti_ TA_GUARDED(lock_);
  ddk::IoBuffer cmd_buffer_ TA_GUARDED(lock_);
  ddk::IoBuffer io_buffer_ TA_GUARDED(lock_);
  size_t io_buffer_size_ = 0u;
  zx::event pipe_event_;

  const ddk::GoldfishPipeProtocolClient* pipe_;
};

}  // namespace goldfish

#endif  // SRC_DEVICES_LIB_GOLDFISH_PIPE_IO_PIPE_IO_H_
