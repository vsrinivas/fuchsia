// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_GOLDFISH_PIPE_IO_PIPE_IO_H_
#define SRC_DEVICES_LIB_GOLDFISH_PIPE_IO_PIPE_IO_H_

#include <fuchsia/hardware/goldfish/pipe/cpp/banjo.h>
#include <lib/ddk/device.h>
#include <lib/ddk/io-buffer.h>
#include <lib/fit/function.h>
#include <lib/fpromise/result.h>
#include <lib/fzl/pinned-vmo.h>
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

  template <class T>
  using ReadContents = std::conditional_t<std::is_same_v<T, char>, std::string, std::vector<T>>;

  template <class T>
  using ReadResult = zx::status<ReadContents<T>>;

  // Read |size| elements of type |T| from the pipe.
  // Returns:
  // - |ZX_OK|: if read succeeds.
  // - |ZX_ERR_SHOULD_WAIT|: only occurs when |blocking| = false, when fewer
  //          than |size| bytes can be read from the pipe without waiting.
  // - Other errors: if pipe read fails, or event wait fails.
  template <class T>
  ReadResult<T> Read(size_t size, bool blocking = false) {
    ReadContents<T> result(size, T{});
    auto read_to_status = ReadTo(result.data(), size * sizeof(T), blocking);
    if (read_to_status != ZX_OK) {
      return zx::error(read_to_status);
    }
    if constexpr (std::is_same_v<T, char>) {
      if (auto pos = result.find('\0'); pos != std::string::npos) {
        result.erase(pos);
      }
    }
    return zx::ok(std::move(result));
  }

  // First read the header for payload size, then read the payload.
  // Returns:
  // - |ZX_OK|: if read succeeds.
  // - |ZX_ERR_SHOULD_WAIT|: only occurs when |blocking| = false, when less than
  //          4 bytes cannot be read (for header), or fewer than |size| bytes
  //          can be read from the pipe without waiting.
  //   Other errors: if pipe read fails, or event wait fails.
  ReadResult<char> ReadWithHeader(bool blocking = false);

  // The source of a pipe Write operation.
  struct WriteSrc {
    // A null-terminated string. The terminator '\0' will be sent to the pipe as
    // well.
    using Str = const std::string_view;
    // A byte vector.
    using Span = const cpp20::span<const uint8_t>;
    // A VMO that is pinned using |PinVmo()| so that pipe device has access to
    // its pages. The VMO must be physically contiguous and pinned using
    // ZX_BTI_CONTIGUOUS option flag.
    using PinnedVmo = struct {
      const fzl::PinnedVmo* vmo;
      size_t offset;
      size_t size;
    };

    std::variant<Str, Span, PinnedVmo> data;
  };

  // Write |sources| to the pipe. All the write operations will be executed
  // in a single pipe command.
  // Returns:
  // - |ZX_OK|: if write succeeds.
  // - |ZX_ERR_SHOULD_WAIT|: only occurs when |blocking| = false, when fewer
  //          than the payload size can be written to the pipe without waiting.
  //   Other errors: if pipe write fails, or event wait fails.
  zx_status_t Write(cpp20::span<const WriteSrc> sources, bool blocking = false);

  // Write a null-terminated string |payload| to the pipe. The terminator '\0'
  // will be sent to the pipe as well.
  // Returns: Same as "Write(cpp20::span<const WriteSrc>, bool)"
  zx_status_t Write(const char* payload, bool blocking = false);

  // Write a byte vector |payload| to the pipe. The terminator '\0'
  // will be sent to the pipe as well.
  // Returns: Same as "Write(cpp20::span<const WriteSrc>, bool)".
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

  // First write all the |write_sources| to the pipe, then read |read_size|
  // elements of type |T| from the pipe. The operations will be executed within
  // a single pipe call command.
  // Returns:
  // - |ZX_OK| and data read from pipe: if write and read succeeds.
  // - |ZX_ERR_SHOULD_WAIT|: only occurs when |blocking| = false, when
  //          |write_sources| cannot be written to the pipe or fewer than
  //          |size| bytes can be read from the pipe without waiting.
  // - Other errors: if pipe write/read fails, or event wait fails.
  template <class T>
  ReadResult<T> Call(cpp20::span<const WriteSrc> write_sources, size_t read_size,
                     bool blocking = false) {
    ReadContents<T> result(read_size, T{});
    auto call_to_status = CallTo(write_sources, result.data(), read_size * sizeof(T), blocking);
    if (call_to_status != ZX_OK) {
      return zx::error(call_to_status);
    }
    if constexpr (std::is_same_v<T, char>) {
      if (auto pos = result.find('\0'); pos != std::string::npos) {
        result.erase(pos);
      }
    }
    return zx::ok(std::move(result));
  }

  // Pin pages of |vmo| and grant goldfish pipe device the ability to access the
  // VMO pages, using |options| as defined in |zx_bti_pin()| syscall.
  // The caller *must* unpin all the VMOs before destroying |PipeIo|.
  fzl::PinnedVmo PinVmo(zx::vmo& vmo, uint32_t options);

  // Pin pages of the range [offset, offset + size) of |vmo| and grant goldfish
  // pipe device the ability to access the VMO pages, using |options| as
  // defined in |zx_bti_pin()| syscall.
  // The caller *must* unpin all the VMOs before destroying |PipeIo|.
  fzl::PinnedVmo PinVmo(zx::vmo& vmo, uint32_t options, size_t offset, size_t size);

  const zx::event& pipe_event() const { return pipe_event_; }
  bool valid() const { return valid_; }

 private:
  zx_status_t Init(const char* pipe_name);
  zx_status_t SetupPipe();

  // Read |size| bytes to |dst|. Caller must make sure that there are |size|
  // bytes allocated at |dst|.
  // Return value: Same as Read().
  zx_status_t ReadTo(void* dst, size_t size, bool blocking = false);

  // Write the |write_sources| and read |size| bytes to |read_dst|.
  // Caller must make sure that there are |size| bytes allocated at |dst|.
  // Return value: Same as Call().
  zx_status_t CallTo(cpp20::span<const WriteSrc> write_sources, void* read_dst, size_t read_size,
                     bool blocking = false);

  zx::status<size_t> ReadOnceLocked(void* buf, size_t size) TA_REQ(lock_);

  struct TransferOp {
    struct IoBuffer {
      size_t offset = 0u;
    };
    struct PinnedVmo {
      size_t paddr = 0u;
    };

    enum class Type { kWrite, kRead } type;
    std::variant<IoBuffer, PinnedVmo> data;
    size_t size;
  };

  // Run a TransferOp command to read / write data stored in IoBuffer or pinned
  // VMO, as specified in |data|.
  zx::status<uint32_t> TransferLocked(const TransferOp& op) TA_REQ(lock_);

  // Run a TransferOp command to read / write multiple data stored in IoBuffer
  // or pinned VMO.
  // If there are both kWrite and kRead in |ops|, all kWrite ops must occur
  // before kRead ops. Otherwise it will return |ZX_ERR_INVALID_ARGS|.
  zx::status<uint32_t> TransferLocked(cpp20::span<const TransferOp> ops) TA_REQ(lock_);

  // Once the command buffer is set up, this runs the transfer command on
  // goldfish pipe device, and will request a wake-up if it gets back pressure.
  zx::status<uint32_t> ExecTransferCommandLocked(bool has_write, bool has_read) TA_REQ(lock_);

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
