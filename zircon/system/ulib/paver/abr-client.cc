// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "abr-client.h"

#include <endian.h>
#include <fuchsia/boot/llcpp/fidl.h>
#include <lib/cksum.h>
#include <lib/fdio/directory.h>
#include <stdio.h>
#include <string.h>

#include "device-partitioner.h"
#include "partition-client.h"
#include "pave-logging.h"

namespace abr {

namespace {

using ::llcpp::fuchsia::paver::Asset;
using ::llcpp::fuchsia::paver::Configuration;

// Extracts value from "zvb.current_slot" argument in boot arguments.
std::optional<std::string_view> GetBootSlot(std::string_view boot_args) {
  for (size_t begin = 0, end;
       (end = boot_args.find_first_of('\0', begin)) != std::string_view::npos; begin = end + 1) {
    const size_t sep = boot_args.find_first_of('=', begin);
    if (sep + 1 < end) {
      std::string_view key(&boot_args[begin], sep - begin);
      if (key.compare("zvb.current_slot") == 0) {
        return std::string_view(&boot_args[sep + 1], end - (sep + 1));
      }
    }
  }
  return std::nullopt;
}

zx_status_t QueryBootConfig(const zx::channel& svc_root, Configuration* out) {
  zx::channel local, remote;
  if (zx_status_t status = zx::channel::create(0, &local, &remote); status != ZX_OK) {
    return status;
  }
  auto status = fdio_service_connect_at(svc_root.get(), ::llcpp::fuchsia::boot::Arguments::Name,
                                        remote.release());
  if (status != ZX_OK) {
    return status;
  }
  ::llcpp::fuchsia::boot::Arguments::SyncClient client(std::move(local));
  auto result = client.Get();
  if (!result.ok()) {
    return result.status();
  }
  const size_t size = result->size;
  if (size == 0) {
    ERROR("Kernel cmdline param zvb.current_slot not found!\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  const auto args_buf = std::make_unique<char[]>(size);
  if (zx_status_t status = result->vmo.read(args_buf.get(), 0, size); status != ZX_OK) {
    return status;
  }

  const auto slot = GetBootSlot(std::string_view(args_buf.get(), size));
  if (!slot.has_value()) {
    ERROR("Kernel cmdline param zvb.current_slot not found!\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (slot->compare("-a") == 0) {
    *out = Configuration::A;
  } else if (slot->compare("-b") == 0) {
    *out = Configuration::B;
  } else if (slot->compare("-r") == 0) {
    *out = Configuration::RECOVERY;
  } else {
    ERROR("Invalid value `%.*s` found in zvb.current_slot!\n", static_cast<int>(slot->size()),
          slot->data());
    return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_OK;
}

zx_status_t SupportsVerfiedBoot(const zx::channel& svc_root) {
  Configuration config;
  if (zx_status_t status = QueryBootConfig(svc_root, &config); status != ZX_OK) {
    return status;
  }
  return ZX_OK;
}

// Implementation of abr::Client which works with a contiguous partition storing abr::Data.
class PartitionClient : public Client {
 public:
  // |partition| should contain abr::Data with no offset.
  static zx_status_t Create(std::unique_ptr<paver::PartitionClient> partition,
                            std::unique_ptr<abr::Client>* out);

  zx_status_t Persist(abr::Data data) override;

  const abr::Data& Data() const override { return data_; }

 private:
  PartitionClient(std::unique_ptr<paver::PartitionClient> partition, zx::vmo vmo, size_t block_size,
                  const abr::Data& data)
      : partition_(std::move(partition)),
        vmo_(std::move(vmo)),
        block_size_(block_size),
        data_(data) {}

  std::unique_ptr<paver::PartitionClient> partition_;
  zx::vmo vmo_;
  size_t block_size_;
  abr::Data data_;
};

zx_status_t PartitionClient::Create(std::unique_ptr<paver::PartitionClient> partition,
                                    std::unique_ptr<abr::Client>* out) {
  size_t block_size;
  if (zx_status_t status = partition->GetBlockSize(&block_size); status != ZX_OK) {
    return status;
  }

  zx::vmo vmo;
  if (zx_status_t status = zx::vmo::create(fbl::round_up(block_size, ZX_PAGE_SIZE), 0, &vmo);
      status != ZX_OK) {
    return status;
  }

  if (zx_status_t status = partition->Read(vmo, block_size); status != ZX_OK) {
    return status;
  }

  abr::Data data;
  if (zx_status_t status = vmo.read(&data, 0, sizeof(data)); status != ZX_OK) {
    return status;
  }

  out->reset(new PartitionClient(std::move(partition), std::move(vmo), block_size, data));
  return ZX_OK;
}

zx_status_t PartitionClient::Persist(abr::Data data) {
  UpdateCrc(&data);
  if (memcmp(&data, &data_, sizeof(data)) == 0) {
    return ZX_OK;
  }
  if (zx_status_t status = vmo_.write(&data, 0, sizeof(data)); status != ZX_OK) {
    return status;
  }
  if (zx_status_t status = partition_->Write(vmo_, block_size_); status != ZX_OK) {
    return status;
  }

  data_ = data;
  return ZX_OK;
}

}  // namespace

zx_status_t Client::Create(fbl::unique_fd devfs_root, zx::channel svc_root,
                           std::unique_ptr<abr::Client>* out) {
  return AstroClient::Create(std::move(devfs_root), std::move(svc_root), out);
}

bool Client::IsValid() const {
  return memcmp(Data().magic, kMagic, kMagicLen) == 0 && Data().version_major == kMajorVersion &&
         Data().version_minor == kMinorVersion && Data().slots[0].priority <= kMaxPriority &&
         Data().slots[1].priority <= kMaxPriority &&
         Data().slots[0].tries_remaining <= kMaxTriesRemaining &&
         Data().slots[1].tries_remaining <= kMaxTriesRemaining &&
         Data().crc32 == htobe32(crc32(0, reinterpret_cast<const uint8_t*>(&Data()),
                                       sizeof(abr::Data) - sizeof(uint32_t)));
}

void Client::UpdateCrc(abr::Data* data) {
  data->crc32 = htobe32(
      crc32(0, reinterpret_cast<const uint8_t*>(data), sizeof(abr::Data) - sizeof(uint32_t)));
}

zx_status_t AstroClient::Create(fbl::unique_fd devfs_root, zx::channel svc_root,
                                std::unique_ptr<abr::Client>* out) {
  if (zx_status_t status = SupportsVerfiedBoot(svc_root); status != ZX_OK) {
    return status;
  }

  std::unique_ptr<paver::DevicePartitioner> partitioner;
  zx_status_t status = paver::SkipBlockDevicePartitioner::Initialize(
      std::move(devfs_root), std::move(svc_root), &partitioner);
  if (status != ZX_OK) {
    return ZX_OK;
  }

  std::unique_ptr<paver::PartitionClient> partition;
  if (zx_status_t status = partitioner->FindPartition(paver::Partition::kABRMeta, &partition);
      status != ZX_OK) {
    return status;
  }

  return PartitionClient::Create(std::move(partition), out);
}

}  // namespace abr
