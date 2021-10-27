// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/inspect/cpp/vmo/block.h>
#include <lib/inspect/cpp/vmo/limits.h>
#include <lib/inspect/cpp/vmo/snapshot.h>
#include <lib/zx/vmar.h>
#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <cstdint>

using inspect::internal::Block;
using inspect::internal::BlockIndex;
using inspect::internal::IndexForOffset;
using inspect::internal::kMinOrderSize;

namespace inspect {

// By default, ensure consistency of the incoming Inspect VMO and retry up to
// 1024 times.
const Snapshot::Options Snapshot::kDefaultOptions = {.read_attempts = 1024,
                                                     .skip_consistency_check = false};

Snapshot::Snapshot(BackingBuffer&& buffer)
    : buffer_(std::make_shared<BackingBuffer>(std::move(buffer))) {}

zx_status_t Snapshot::Create(BackingBuffer&& buffer, Snapshot* out_snapshot) {
  ZX_ASSERT(out_snapshot);

  if (buffer.Size() < kMinOrderSize) {
    return ZX_ERR_INVALID_ARGS;
  }

  // A buffer does not have concurrent writers or observers, so we don't use
  // this.
  uint64_t unused;
  // Verify that the buffer can, in fact, be parsed as a snapshot.
  zx_status_t status = Snapshot::ParseHeader(buffer.Data(), &unused);
  if (status != ZX_OK) {
    return status;
  }
  *out_snapshot = Snapshot(std::move(buffer));
  if (!*out_snapshot) {
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

zx_status_t Snapshot::Create(const zx::vmo& vmo, Snapshot* out_snapshot) {
  return Snapshot::Create(vmo, kDefaultOptions, out_snapshot);
}

zx_status_t Snapshot::Create(const zx::vmo& vmo, Options options, Snapshot* out_snapshot) {
  return Snapshot::Create(vmo, options, nullptr, out_snapshot);
}

zx_status_t Snapshot::Create(const zx::vmo& vmo, Options options, ReadObserver read_observer,
                             Snapshot* out_snapshot) {
  uint64_t generation;
  BackingBuffer maybe_frozen(vmo);
  Snapshot::ParseHeader(maybe_frozen.Data(), &generation);
  if (generation == internal::kVmoFrozen) {
    if (read_observer) {
      read_observer(maybe_frozen.Data(), maybe_frozen.Size());
    }

    *out_snapshot = Snapshot(std::move(maybe_frozen));
    return ZX_OK;
  }

  size_t tries_left = options.read_attempts;

  zx_status_t status;
  std::vector<uint8_t> buffer;

  while (tries_left-- > 0) {
    size_t size;
    status = vmo.get_size(&size);
    if (status != ZX_OK) {
      return status;
    }
    if (size < sizeof(Block)) {
      return ZX_ERR_OUT_OF_RANGE;
    }
    if (buffer.size() != size) {
      buffer.resize(size);
    }

    status = Snapshot::Read(vmo, sizeof(Block), buffer.data());
    if (status != ZX_OK) {
      return status;
    }
    if (read_observer) {
      read_observer(buffer.data(), sizeof(Block));
    }

    status = Snapshot::ParseHeader(buffer.data(), &generation);
    if (status != ZX_OK) {
      return status;
    }

    if (!options.skip_consistency_check && generation % 2 != 0) {
      continue;
    }

    status = Snapshot::Read(vmo, size, buffer.data());
    if (status != ZX_OK) {
      return status;
    }
    if (read_observer) {
      read_observer(buffer.data(), size);
    }

    // Read the header out of the buffer again,
    std::vector<uint8_t> new_header(sizeof(Block));
    status = Snapshot::Read(vmo, new_header.size(), new_header.data());
    if (status != ZX_OK) {
      return status;
    }
    if (read_observer) {
      read_observer(new_header.data(), new_header.size());
    }

    uint64_t new_generation;
    status = Snapshot::ParseHeader(new_header.data(), &new_generation);
    if (status != ZX_OK) {
      return status;
    }
    if (!options.skip_consistency_check && generation != new_generation) {
      continue;
    }

    size_t new_size;
    if (vmo.get_size(&new_size) != ZX_OK) {
      return ZX_ERR_INTERNAL;
    }
    if (new_size != size) {
      continue;
    }

    *out_snapshot = Snapshot(BackingBuffer(std::move(buffer)));

    return ZX_OK;
  }

  return ZX_ERR_INTERNAL;
}

zx_status_t Snapshot::Read(const zx::vmo& vmo, size_t size, uint8_t* buffer) {
  memset(buffer, 0, size);
  return vmo.read(buffer, 0, size);
}

zx_status_t Snapshot::ParseHeader(const uint8_t* buffer, uint64_t* out_generation_count) {
  auto* block = reinterpret_cast<const Block*>(buffer);
  if (memcmp(&block->header_data[4], internal::kMagicNumber, 4) != 0 ||
      internal::HeaderBlockFields::Version::Get<uint64_t>(block->header) > internal::kVersion) {
    return ZX_ERR_INTERNAL;
  }
  *out_generation_count = block->payload.u64;
  return ZX_OK;
}

namespace internal {
const Block* GetBlock(const Snapshot* snapshot, BlockIndex index) {
  // Check that the block's index fits in the snapshot.
  // This means that the whole first 16 bytes of the block are valid to read.
  if (index >= IndexForOffset(snapshot->size())) {
    return nullptr;
  }
  const auto* ret = reinterpret_cast<const Block*>(snapshot->data() + index * kMinOrderSize);

  // Check that the entire declared size of the block fits in the snapshot.
  auto size = OrderToSize(GetOrder(ret));
  if (index * kMinOrderSize + size > snapshot->size()) {
    return nullptr;
  }

  return ret;
}
}  // namespace internal

BackingBuffer::BackingBuffer(const zx::vmo& data) {
  zx::vmar to_hold_data;
  uintptr_t data_ptr;
  auto status = data.get_size(&size_);
  ZX_ASSERT(ZX_OK == status);

  zx::vmar::root_self()->allocate(ZX_VM_CAN_MAP_READ, 0, size_, &to_hold_data, &data_ptr);

  data_ = std::make_pair(0, std::move(to_hold_data));
  auto& data_ref = cpp17::get<DiscriminateData::kMapping>(data_);
  status = data_ref.second.map(ZX_VM_PERM_READ, 0, data, 0, size_, &data_ref.first);
  ZX_ASSERT(ZX_OK == status);
}

BackingBuffer::~BackingBuffer() {
  switch (Index()) {
    case DiscriminateData::kVector:
      break;
    case DiscriminateData::kMapping:
      const auto& data_ref = cpp17::get<DiscriminateData::kMapping>(data_);
      data_ref.second.unmap(data_ref.first, size_);
      break;
  }
}

const uint8_t* BackingBuffer::Data() const {
  switch (Index()) {
    case DiscriminateData::kVector:
      return cpp17::get<DiscriminateData::kVector>(data_).data();
    case DiscriminateData::kMapping:
      return reinterpret_cast<const uint8_t*>(cpp17::get<DiscriminateData::kMapping>(data_).first);
  }
}

size_t BackingBuffer::Size() const { return size_; }

bool BackingBuffer::Empty() const {
  switch (Index()) {
    case DiscriminateData::kVector:
      return cpp17::get<DiscriminateData::kVector>(data_).empty();
    case DiscriminateData::kMapping:
      return size_ == 0;
  }
}
}  // namespace inspect
