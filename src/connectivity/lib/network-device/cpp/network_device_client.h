// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_LIB_NETWORK_DEVICE_CPP_NETWORK_DEVICE_CLIENT_H_
#define SRC_CONNECTIVITY_LIB_NETWORK_DEVICE_CPP_NETWORK_DEVICE_CLIENT_H_

#include <fuchsia/hardware/network/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/cpp/wait.h>
#include <lib/fzl/vmo-mapper.h>
#include <zircon/device/network.h>

#include <queue>

#include <fbl/span.h>
#include <src/lib/fxl/macros.h>

namespace network {
namespace client {

namespace netdev = fuchsia::hardware::network;

// Configuration for sessions opened by a `NetworkDeviceClient`.
struct SessionConfig {
  // Length of each buffer, in bytes.
  uint32_t buffer_length;
  // Buffer stride on VMO, in bytes.
  uint64_t buffer_stride;
  // Descriptor length, in bytes.
  uint64_t descriptor_length;
  // Number of rx descriptors to allocate.
  uint16_t rx_descriptor_count;
  // Number of tx descriptors to allocate.
  uint16_t tx_descriptor_count;
  // Session flags.
  netdev::SessionFlags options;
  // Types of rx frames to subscribe to.
  std::vector<netdev::FrameType> rx_frames;
};

// A client for `fuchsia.hardware.network/Device`.
class NetworkDeviceClient {
 public:
  class Buffer;
  class BufferData;
  class StatusWatchHandle;

  // Creates a default session configuration with the given device information.
  static SessionConfig DefaultSessionConfig(const netdev::Info& dev_info);
  // Creates a client that will bind to `handle` using `dispatcher`.
  // If `dispatcher` is `nullptr`, the default dispatcher for the current thread will be used.
  // All the `NetworkDeviceClient` callbacks are called on the dispatcher.
  explicit NetworkDeviceClient(fidl::InterfaceHandle<netdev::Device> handle,
                               async_dispatcher_t* dispatcher = nullptr);
  ~NetworkDeviceClient();

  using OpenSessionCallback = fit::function<void(zx_status_t)>;
  using SessionConfigFactory = fit::function<SessionConfig(const netdev::Info&)>;
  using RxCallback = fit::function<void(Buffer buffer)>;
  using ErrorCallback = fit::function<void(zx_status_t err)>;
  using StatusCallback = fit::function<void(netdev::Status)>;
  // Opens a new session with `name` and invokes `callback` when done.
  // `config_factory` is called to create a `SessionConfig` from a `fuchsia.hardware.network/Info`,
  // which is used to define the buffer allocation and layout for the new session.
  // If the client already has a session running, `OpenSession` fails with `ZX_ERR_ALREADY_EXISTS`.
  void OpenSession(const std::string& name, OpenSessionCallback callback,
                   SessionConfigFactory config_factory = NetworkDeviceClient::DefaultSessionConfig);

  // Sets the callback for received frames.
  void SetRxCallback(RxCallback callback) { rx_callback_ = std::move(callback); }

  // Sets the callback that will be triggered when an open session encounters errors or the device
  // handle is closed.
  void SetErrorCallback(ErrorCallback callback) { err_callback_ = std::move(callback); }

  // Pauses or unpauses this client's session.
  // Returns `ZX_ERR_BAD_STATE` if there's no open session.
  [[nodiscard]] zx_status_t SetPaused(bool paused);
  // Kills the current session.
  // The error callback is called once the session is destroyed with the session epitaph.
  // Returns `ZX_ERR_BAD_STATE` if there's no open session.
  [[nodiscard]] zx_status_t KillSession();

  // Attempts to send `buffer`.
  // If this buffer was received on the rx path, `Send` will attempt to allocate a buffer from the
  // transmit pool to replace this buffer before sending it.
  // `buffer` is transitioned to an invalid state on success.
  [[nodiscard]] zx_status_t Send(Buffer* buffer);

  bool HasSession() { return session_.is_bound(); }

  // Creates an asynchronous handler for status changes.
  // `callback` will be called for every status change on the device for as long as the returned
  // `StatusWatchHandle` is in scope.
  // `buffer` is the number of changes buffered by the network device, according to the
  // `fuchsia.hardware.network.Device` protocol.
  StatusWatchHandle WatchStatus(StatusCallback callback, uint32_t buffer = 1);

  const netdev::Info& device_info() const { return device_info_; }

  // Allocates a transmit buffer.
  // Takes a buffer from the pool of available transmit buffers. If there are no buffers available,
  // the returned `Buffer` instance will be invalid (`Buffer::is_valid` returns false).
  Buffer AllocTx();

  // A contiguous buffer region.
  // `Buffer`s are composed of N disjoint `BufferRegion`s, accessible through `BufferData`.
  class BufferRegion {
   public:
    // Caps this buffer to `len` bytes.
    // If the buffer's length is smaller than `len`, `CapLength` does nothing.
    void CapLength(uint32_t len);
    uint32_t len() const;
    fbl::Span<uint8_t> data();
    fbl::Span<const uint8_t> data() const;
    // Writes `len` bytes from `src` into this region starting at `offset`.
    // Returns the number of bytes that were written.
    size_t Write(const void* src, size_t len, size_t offset = 0);
    // Reads `len` bytes from this region into `dst` starting at `offset`.
    // Returns the number of bytes that were read.
    size_t Read(void* dst, size_t len, size_t offset = 0);
    // Writes the `src` region starting at `src_offset` into this region starting at `offset`.
    // Returns the number of bytes written.
    size_t Write(size_t offset, const BufferRegion& src, size_t src_offset);
    // Move assignment deleted to abide by `Buffer` destruction semantics. See `Buffer` for more
    // details.
    BufferRegion& operator=(BufferRegion&&) = delete;
    BufferRegion(BufferRegion&& other) = default;

   protected:
    friend BufferData;
    BufferRegion() = default;

   private:
    void* base_;
    buffer_descriptor_t* desc_;

    FXL_DISALLOW_COPY_AND_ASSIGN(BufferRegion);
  };

  // A collection of `BufferRegion`s and metadata associated with a `Buffer`.
  class BufferData {
   public:
    // Returns true if this `BufferData` contains at least 1 `BufferRegion`.
    bool is_loaded() { return parts_count_ != 0; }
    // The number of `BufferRegion`s in this `BufferData`.
    uint32_t parts() { return parts_count_; }

    // Retrieves a `BufferRegion` part from this `BufferData`.
    // Crashes if `idx >= parts()`.
    BufferRegion& part(size_t idx);
    const BufferRegion& part(size_t idx) const;
    // The total length, in bytes, of the buffer.
    uint32_t len() const;

    netdev::FrameType frame_type() const;
    netdev::InfoType info_type() const;
    uint32_t inbound_flags() const;
    uint32_t return_flags() const;

    void SetFrameType(netdev::FrameType type);
    void SetTxRequest(netdev::TxFlags tx_flags);

    // Writes up to `len` bytes from `src` into the buffer, returning the number of bytes written.
    size_t Write(const void* src, size_t len);
    // Reads up to `len` bytes from the buffer into `dst`, returning the number of bytes read.
    size_t Read(void* dst, size_t len);
    // Writes the contents of `data` into this buffer, returning the number of bytes written.
    size_t Write(const BufferData& data);

    // Move assignment deleted to abide by `Buffer` destruction semantics. See `Buffer` for more
    // details.
    BufferData& operator=(BufferData&&) = delete;

   protected:
    friend Buffer;
    BufferData() = default;
    BufferData(BufferData&& other) = default;

    // Loads buffer information from `parent` using the descriptor `idx`.
    void Load(NetworkDeviceClient* parent, uint16_t idx);

   private:
    uint32_t parts_count_ = 0;
    std::array<BufferRegion, netdev::MAX_DESCRIPTOR_CHAIN> parts_{};

    FXL_DISALLOW_COPY_AND_ASSIGN(BufferData);
  };

  class Buffer {
   public:
    Buffer();
    ~Buffer();
    Buffer(Buffer&& other) noexcept;
    // NOTE: A `Buffer` is returned to its parent on destruction. We'd have to do the same thing
    // with the target buffer on the move assignment operator, which can be very counter-intuitive.
    // We delete the move assignment operator to avoid confusion and misuse.
    Buffer& operator=(Buffer&&) = delete;

    bool is_valid() const { return parent_ != nullptr; }

    BufferData& data();
    const BufferData& data() const;

    // Equivalent to calling `Send(buffer)` on this buffer's `NetworkDeviceClient` parent.
    [[nodiscard]] zx_status_t Send();

   protected:
    friend NetworkDeviceClient;
    Buffer(NetworkDeviceClient* parent, uint16_t descriptor, bool rx);

   private:
    // Pointer device that owns this buffer, not owned.
    // A buffer with a null parent is an invalid buffer.
    NetworkDeviceClient* parent_;
    uint16_t descriptor_;
    bool rx_;
    mutable BufferData data_;

    FXL_DISALLOW_COPY_AND_ASSIGN(Buffer);
  };

  // An RAII object that triggers a callback on NetworkDevice status changes.
  class StatusWatchHandle {
   protected:
    friend NetworkDeviceClient;

    StatusWatchHandle(fidl::InterfaceHandle<netdev::StatusWatcher> watcher, StatusCallback callback)
        : watcher_(watcher.Bind()), callback_(std::move(callback)) {
      Watch();
    }

   private:
    void Watch();
    netdev::StatusWatcherPtr watcher_;
    StatusCallback callback_;
  };

 private:
  zx_status_t PrepareSession();
  zx_status_t MakeSessionInfo(netdev::SessionInfo* session_info);
  zx_status_t PrepareDescriptors();
  buffer_descriptor_t* descriptor(uint16_t idx);
  void* data(uint64_t offset);
  void ResetRxDescriptor(buffer_descriptor_t* desc);
  void ResetTxDescriptor(buffer_descriptor_t* desc);
  void ReturnTxDescriptor(uint16_t desc);
  void ReturnRxDescriptor(uint16_t desc);
  void FlushRx();
  void FlushTx();
  void FetchRx();
  void FetchTx();
  void TxSignal(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                const zx_packet_signal_t* signal);
  void RxSignal(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                const zx_packet_signal_t* signal);
  void ErrorTeardown(zx_status_t err);

  bool session_running_ = false;
  RxCallback rx_callback_;
  ErrorCallback err_callback_;
  async_dispatcher_t* dispatcher_;
  SessionConfig session_config_;
  uint16_t descriptor_count_;
  zx::vmo data_vmo_;
  fzl::VmoMapper data_;
  zx::vmo descriptors_vmo_;
  fzl::VmoMapper descriptors_;
  zx::fifo rx_fifo_;
  zx::fifo tx_fifo_;
  netdev::DevicePtr device_;
  netdev::SessionPtr session_;
  netdev::Info device_info_;
  // rx descriptors ready to be sent back to the device.
  std::vector<uint16_t> rx_out_queue_;
  // tx descriptors available to be written.
  std::queue<uint16_t> tx_avail_;
  // tx descriptors queued to be sent to device.
  std::vector<uint16_t> tx_out_queue_;

  async::WaitMethod<NetworkDeviceClient, &NetworkDeviceClient::TxSignal> tx_wait_{this};
  async::WaitMethod<NetworkDeviceClient, &NetworkDeviceClient::RxSignal> rx_wait_{this};
  async::WaitMethod<NetworkDeviceClient, &NetworkDeviceClient::TxSignal> tx_writable_wait_{this};
  async::WaitMethod<NetworkDeviceClient, &NetworkDeviceClient::RxSignal> rx_writable_wait_{this};

  std::unique_ptr<async::Executor> executor_;

  FXL_DISALLOW_COPY_AND_ASSIGN(NetworkDeviceClient);
};

}  // namespace client
}  // namespace network

#endif  // SRC_CONNECTIVITY_LIB_NETWORK_DEVICE_CPP_NETWORK_DEVICE_CLIENT_H_
