// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_BUFFER_H_
#define SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_BUFFER_H_

#include <fuchsia/net/tun/cpp/fidl.h>
#include <lib/fzl/vmo-mapper.h>

#include <array>

#include <ddktl/protocol/network/device.h>
#include <fbl/span.h>

namespace network {
namespace tun {

class Buffer;

// A data structure that stores keyed VMOs and allocates buffers.
//
// `VmoStore` stores up to `MAX_VMOS` VMOs keyed by an identifier bound to the range [0,
// `MAX_VMOS`). `VmoStore` can be used to allocate buffers backed by the VMOs it contains.
//
// This class is used to fulfill the VMO registration mechanism used by
// `ddk.protocol.network.device`.
class VmoStore {
 public:
  ~VmoStore() = default;
  // Reads `len` bytes at `offset` from the VMO identified by `id` into `data`, which must be a
  // `uint8_t` iterator.
  // Returns an error if the specified region is invalid or `id` is not registered.
  template <class T>
  zx_status_t Read(uint8_t id, size_t offset, size_t len, T data) {
    fbl::Span<uint8_t> vmo_data;
    zx_status_t status = GetMappedVmo(id, &vmo_data);
    if (status != ZX_OK) {
      return status;
    }
    if (offset + len > vmo_data.size()) {
      return ZX_ERR_OUT_OF_RANGE;
    }
    std::copy_n(vmo_data.begin() + offset, len, data);
    return ZX_OK;
  }

  // Writes `len` bytes at `offset` into the VMO identified by `id` from `data`, which must be an
  // `uint8_t` iterator.
  // Returns an error if the specified region is invalid or `id` is not registered.
  template <class T>
  zx_status_t Write(uint8_t id, size_t offset, size_t len, T data) {
    fbl::Span<uint8_t> vmo_data;
    zx_status_t status = GetMappedVmo(id, &vmo_data);
    if (status != ZX_OK) {
      return status;
    }
    if (offset + len > vmo_data.size()) {
      return ZX_ERR_OUT_OF_RANGE;
    }
    std::copy_n(data, len, vmo_data.begin() + offset);
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
  static zx_status_t Copy(VmoStore* src_store, uint8_t src_id, size_t src_offset,
                          VmoStore* dst_store, uint8_t dst_id, size_t dst_offset, size_t len);

  Buffer MakeTxBuffer(const tx_buffer_t* tx, bool get_meta);
  Buffer MakeRxSpaceBuffer(const rx_space_buffer_t* space);

 private:
  struct StoredVmo {
    zx::vmo vmo;
    fzl::VmoMapper mapper;
  };
  zx_status_t GetMappedVmo(uint8_t id, fbl::Span<uint8_t>* out_span);
  std::array<fit::optional<StoredVmo>, MAX_VMOS> vmos_;
};

// A device buffer.
// Device buffers can be created from VMO stores. They're used to store references to buffers
// retrieved from a NetworkDeviceInterface, which point to data regions within a VMO.
// `Buffer` can represent either a tx (application-filled data) buffer or an rx (empty space for
// inbound data) buffer.
class Buffer {
 public:
  // Reads this buffer's data into `vec`.
  // Used to serve `fuchsia.net.tun/Device.ReadFrame`.
  // Returns an error if this buffer's definition does not map to valid data (see `VmoStore::Write`
  // for specific error codes).
  zx_status_t Read(std::vector<uint8_t>* vec);
  // Writes `data` into this buffer.
  // If this `data` does not fit in this buffer, `ZX_ERR_OUT_OF_RANGE` is returned.
  // Returns an error if this buffer's definition does not map to valid data (see `VmoStore::Write`
  // for specific error codes).
  // Used to serve `fuchsia.net.tun/Device.WriteFrame`.
  zx_status_t Write(const std::vector<uint8_t>& data);
  // Copies data from `other` into this buffer, returning the number of bytes written in `total`.
  zx_status_t CopyFrom(Buffer* other, size_t* total);

  inline fuchsia::hardware::network::FrameType frame_type() const { return frame_type_.value(); }

  inline uint32_t id() const { return id_; }

  inline std::unique_ptr<fuchsia::net::tun::FrameMetadata> TakeMetadata() {
    return std::move(meta_);
  }

 protected:
  friend VmoStore;
  // Creates a device buffer from a tx request buffer.
  Buffer(const tx_buffer_t* tx, bool get_meta, VmoStore* vmo_store);
  // Creates a device buffer from an rx space buffer.
  Buffer(const rx_space_buffer_t* space, VmoStore* vmo_store);

 private:
  const uint32_t id_{};
  // Pointer to parent VMO store, not owned.
  VmoStore* const vmo_store_;
  const uint8_t vmo_id_;
  std::array<buffer_region_t, MAX_VIRTUAL_PARTS> parts_{};
  const size_t parts_count_{};
  std::unique_ptr<fuchsia::net::tun::FrameMetadata> meta_;
  const fit::optional<fuchsia::hardware::network::FrameType> frame_type_;
};

}  // namespace tun
}  // namespace network

#endif  // SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_BUFFER_H_
