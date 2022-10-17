// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_BUFFER_H_
#define SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_BUFFER_H_

#include <fidl/fuchsia.net.tun/cpp/wire.h>
#include <fuchsia/hardware/network/device/cpp/banjo.h>
#include <lib/stdcompat/span.h>

#include <array>

#include "src/lib/vmo_store/vmo_store.h"

namespace network {
namespace tun {

class TxBuffer;
class RxBuffer;

// A data structure that stores keyed VMOs and allocates buffers.
//
// `VmoStore` stores up to `MAX_VMOS` VMOs keyed by an identifier bound to the range [0,
// `MAX_VMOS`). `VmoStore` can be used to allocate buffers backed by the VMOs it contains.
//
// This class is used to fulfill the VMO registration mechanism used by
// `fuchsia.hardware.network.device`.
class VmoStore {
 public:
  VmoStore()
      : store_(vmo_store::Options{
            .map =
                vmo_store::MapOptions{
                    .vm_option = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_REQUIRE_NON_RESIZABLE,
                },
        }) {}

  ~VmoStore() = default;

  // Reads `len` bytes at `offset` from the VMO identified by `id` into `data`, which must be a
  // `uint8_t` iterator.
  // Returns an error if the specified region is invalid or `id` is not registered.
  template <class T>
  zx_status_t Read(uint8_t id, size_t offset, size_t len, T data) {
    zx::result vmo_data = GetMappedVmo(id);
    if (vmo_data.is_error()) {
      return vmo_data.status_value();
    }
    if (offset + len > vmo_data->size()) {
      return ZX_ERR_OUT_OF_RANGE;
    }
    std::copy_n(vmo_data->begin() + offset, len, data);
    return ZX_OK;
  }

  // Writes `len` bytes at `offset` into the VMO identified by `id` from `data`, which must be an
  // `uint8_t` iterator.
  // Returns an error if the specified region is invalid or `id` is not registered.
  template <class T>
  zx_status_t Write(uint8_t id, size_t offset, size_t len, T data) {
    zx::result vmo_data = GetMappedVmo(id);
    if (vmo_data.is_error()) {
      return vmo_data.status_value();
    }
    if (offset + len > vmo_data->size()) {
      return ZX_ERR_OUT_OF_RANGE;
    }
    std::copy_n(data, len, vmo_data->begin() + offset);
    return ZX_OK;
  }

  // Registers and maps `vmo` identified by `id`.
  // `id` comes from a `NetworkDeviceInterface` and is part of the NetworkDevice contract.
  // Returns an error if the identifier is invalid or already in use, or the mapping fails.
  zx_status_t RegisterVmo(uint8_t id, zx::vmo vmo);
  // Unregister a previously registered VMO with `id`, unmapping it from memory and releasing the
  // VMO handle.
  // Returns an error if the identifier is invalid or does not map to a registered VMO.
  zx_status_t UnregisterVmo(uint8_t id);

  // Copies `len` bytes from `src_store`'s VMO with `src_id` at `src_offset` to `dst_store`'s VMO
  // with `dst_id` at `dst_offset`.
  //
  // Equivalent to:
  // T data;
  // src_store.Read(src_id, src_offset, len, back_inserter(data));
  // dst_store.Write(dst_id, dst_offset, len, data.begin());
  static zx_status_t Copy(VmoStore& src_store, uint8_t src_id, size_t src_offset,
                          VmoStore& dst_store, uint8_t dst_id, size_t dst_offset, size_t len);

  TxBuffer MakeTxBuffer(const tx_buffer_t& tx, bool get_meta);
  RxBuffer MakeRxSpaceBuffer(const rx_space_buffer_t& space);
  RxBuffer MakeEmptyRxBuffer();

 private:
  zx::result<cpp20::span<uint8_t>> GetMappedVmo(uint8_t id);
  vmo_store::VmoStore<vmo_store::SlabStorage<uint8_t>> store_;
};

// A device buffer.
// Device buffers can be created from VMO stores. They're used to store references to buffers
// retrieved from a NetworkDeviceInterface, which point to data regions within a VMO.
// `Buffer` can represent either a tx (application-filled data) buffer or an rx (empty space for
// inbound data) buffer.
class Buffer {
 public:
  Buffer(Buffer&&) = default;
  Buffer(const Buffer&) = delete;

  // Reads this buffer's data into `vec`.
  // Used to serve `fuchsia.net.tun/Device.ReadFrame`.
  // Returns an error if this buffer's definition does not map to valid data (see `VmoStore::Write`
  // for specific error codes).
  zx_status_t Read(std::vector<uint8_t>& vec);
  // Writes `data` into this buffer.
  // If this `data` does not fit in this buffer, `ZX_ERR_OUT_OF_RANGE` is returned.
  // Returns an error if this buffer's definition does not map to valid data (see `VmoStore::Write`
  // for specific error codes).
  // Used to serve `fuchsia.net.tun/Device.WriteFrame`.
  zx_status_t Write(const uint8_t* data, size_t count);
  zx_status_t Write(const fidl::VectorView<uint8_t>& data);
  zx_status_t Write(const std::vector<uint8_t>& data);
  // Copies data from `other` into this buffer, returning the number of bytes written on success.
  zx::result<size_t> CopyFrom(Buffer& other);
  // Returns this buffer's length in bytes.
  uint64_t length() const { return total_length_; }

 protected:
  struct BufferPart {
    uint32_t buffer_id;
    buffer_region_t region;
  };

  void PushPart(const BufferPart& part);
  explicit Buffer(VmoStore* vmo_store) : vmo_store_(vmo_store) {}

  cpp20::span<BufferPart> parts() { return cpp20::span(parts_.data(), parts_count_); }
  cpp20::span<const BufferPart> parts() const { return cpp20::span(parts_.data(), parts_count_); }

 private:
  // Pointer to parent VMO store, not owned.
  VmoStore* const vmo_store_;
  std::array<BufferPart, MAX_BUFFER_PARTS> parts_;
  size_t parts_count_ = 0;
  uint64_t total_length_ = 0;
};

class TxBuffer : public Buffer {
 public:
  inline fuchsia_hardware_network::wire::FrameType frame_type() const { return frame_type_; }
  uint32_t id() const { return parts().begin()->buffer_id; }
  uint8_t port_id() const { return port_id_; }

  inline std::optional<fuchsia_net_tun::wire::FrameMetadata> TakeMetadata() {
    return std::exchange(meta_, std::nullopt);
  }

 protected:
  friend VmoStore;
  TxBuffer(const tx_buffer_t& tx, bool get_meta, VmoStore* vmo_store);

 private:
  const uint8_t port_id_;
  const fuchsia_hardware_network::wire::FrameType frame_type_;
  std::optional<fuchsia_net_tun::wire::FrameMetadata> meta_;
};

class RxBuffer : public Buffer {
 public:
  // Adds more rx buffer space to this buffer.
  void PushRxSpace(const rx_space_buffer_t& space);

  // Calls the provided closure function once for each buffer space in this RxBuffer.
  template <typename F>
  void WithSpace(F fn) {
    for (BufferPart& part : parts()) {
      fn(rx_space_buffer_t{
          .id = part.buffer_id,
          .region = part.region,
      });
    }
  }

  // Calls the provided closure function once for each return buffer part in this RxBuffer. The
  // total length of the buffer parts is capped to `written_length` bytes.
  template <typename F>
  void WithReturn(size_t written_length, F fn) {
    for (BufferPart& part : parts()) {
      size_t length = std::min(written_length, part.region.length);
      // Length is expected to be less than max uint32 because of max MTU, but guard it here with a
      // debug assertion so the cast is still clearly safe.
      ZX_DEBUG_ASSERT(length <= std::numeric_limits<uint32_t>::max());
      fn(rx_buffer_part_t{
          .id = part.buffer_id,
          .length = static_cast<uint32_t>(length),
      });
      written_length -= length;
    }
  }

 protected:
  friend VmoStore;
  explicit RxBuffer(VmoStore* vmo_store) : Buffer(vmo_store) {}
};

}  // namespace tun
}  // namespace network

#endif  // SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_BUFFER_H_
