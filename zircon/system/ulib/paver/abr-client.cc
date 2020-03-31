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
#include <zircon/status.h>

#include <libabr/libabr.h>

#include <string_view>

#include "device-partitioner.h"
#include "partition-client.h"
#include "pave-logging.h"
#include "zircon/errors.h"

namespace abr {

namespace {

using ::llcpp::fuchsia::paver::Asset;
using ::llcpp::fuchsia::paver::Configuration;

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
  auto result = client.GetString(::fidl::StringView{"zvb.current_slot"});
  if (!result.ok()) {
    return result.status();
  }

  const auto response = result.Unwrap();
  if (response->value.is_null()) {
    ERROR("Kernel cmdline param zvb.current_slot not found!\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto slot = std::string_view{response->value.data(), response->value.size()};
  // Some bootloaders prefix slot with dash or underscore. We strip them for consistency.
  slot.remove_prefix(std::min(slot.find_first_not_of("_-"), slot.size()));
  if (slot.compare("a") == 0) {
    *out = Configuration::A;
  } else if (slot.compare("b") == 0) {
    *out = Configuration::B;
  } else if (slot.compare("r") == 0) {
    *out = Configuration::RECOVERY;
  } else {
    ERROR("Invalid value `%.*s` found in zvb.current_slot!\n", static_cast<int>(slot.size()),
          slot.data());
    return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_OK;
}

zx_status_t SupportsVerifiedBoot(const zx::channel& svc_root) {
  Configuration config;
  if (zx_status_t status = QueryBootConfig(svc_root, &config); status != ZX_OK) {
    return status;
  }
  return ZX_OK;
}

// Implementation of abr::Client which works with a contiguous partition storing AbrData.
class PartitionClient : public Client {
 public:
  // |partition| should contain AbrData with no offset.
  static zx_status_t Create(std::unique_ptr<paver::PartitionClient> partition,
                            std::unique_ptr<abr::Client>* out);

 private:
  PartitionClient(std::unique_ptr<paver::PartitionClient> partition, zx::vmo vmo, size_t block_size)
      : partition_(std::move(partition)), vmo_(std::move(vmo)), block_size_(block_size) {}

  std::unique_ptr<paver::PartitionClient> partition_;
  zx::vmo vmo_;
  size_t block_size_;

  zx_status_t Read(uint8_t* buffer, size_t size) override;

  zx_status_t Write(const uint8_t* buffer, size_t size) override;
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

  out->reset(new PartitionClient(std::move(partition), std::move(vmo), block_size));
  return ZX_OK;
}

zx_status_t PartitionClient::Read(uint8_t* buffer, size_t size) {
  if (zx_status_t status = partition_->Read(vmo_, block_size_); status != ZX_OK) {
    return status;
  }
  if (zx_status_t status = vmo_.read(buffer, 0, size); status != ZX_OK) {
    return status;
  }
  return ZX_OK;
}

zx_status_t PartitionClient::Write(const uint8_t* buffer, size_t size) {
  if (zx_status_t status = vmo_.write(buffer, 0, size); status != ZX_OK) {
    return status;
  }
  if (zx_status_t status = partition_->Write(vmo_, block_size_); status != ZX_OK) {
    return status;
  }
  if (zx_status_t status = partition_->Flush(); status != ZX_OK) {
    return status;
  }
  return ZX_OK;
}

}  // namespace

zx_status_t Client::Create(fbl::unique_fd devfs_root, const zx::channel& svc_root,
                           std::unique_ptr<abr::Client>* out) {
  if (zx_status_t status = SupportsVerifiedBoot(svc_root); status != ZX_OK) {
    return status;
  }

  if (AstroClient::Create(devfs_root.duplicate(), out) == ZX_OK ||
      SherlockClient::Create(std::move(devfs_root), out) == ZX_OK) {
    (*out)->abr_ops_ = {out->get(), Client::ReadAbrMetaData, Client::WriteAbrMetaData};
    return ZX_OK;
  }

  return ZX_ERR_NOT_FOUND;
}

bool Client::ReadAbrMetaData(void* context, size_t size, uint8_t* buffer) {
  if (auto res = static_cast<Client*>(context)->Read(buffer, size); res != ZX_OK) {
    ERROR("Failed to read abr data from storage. %s\n", zx_status_get_string(res));
    return false;
  }
  return true;
}

bool Client::WriteAbrMetaData(void* context, const uint8_t* buffer, size_t size) {
  if (auto res = static_cast<Client*>(context)->Write(buffer, size); res != ZX_OK) {
    ERROR("Failed to write abr data to storage. %s\n", zx_status_get_string(res));
    return false;
  }
  return true;
}

zx_status_t Client::AbrResultToZxStatus(AbrResult status) {
  switch (status) {
    case kAbrResultOk:
      return ZX_OK;
    case kAbrResultErrorIo:
      return ZX_ERR_IO;
    case kAbrResultErrorInvalidData:
      return ZX_ERR_INVALID_ARGS;
    case kAbrResultErrorUnsupportedVersion:
      return ZX_ERR_NOT_SUPPORTED;
  }
  ERROR("Unknown Abr result code %d!\n", status);
  return ZX_ERR_INTERNAL;
}

zx_status_t AstroClient::Create(fbl::unique_fd devfs_root, std::unique_ptr<abr::Client>* out) {
  std::unique_ptr<paver::DevicePartitioner> partitioner;
  zx_status_t status = paver::AstroPartitioner::Initialize(std::move(devfs_root), &partitioner);
  if (status != ZX_OK) {
    return status;
  }

  // ABR metadata has no need of a content type since it's always local rather
  // than provided in an update package, so just use the default content type.
  std::unique_ptr<paver::PartitionClient> partition;
  if (zx_status_t status =
          partitioner->FindPartition(paver::PartitionSpec(paver::Partition::kAbrMeta), &partition);
      status != ZX_OK) {
    return status;
  }

  return PartitionClient::Create(std::move(partition), out);
}

zx_status_t SherlockClient::Create(fbl::unique_fd devfs_root, std::unique_ptr<abr::Client>* out) {
  std::unique_ptr<paver::DevicePartitioner> partitioner;
  zx_status_t status =
      paver::SherlockPartitioner::Initialize(std::move(devfs_root), std::nullopt, &partitioner);
  if (status != ZX_OK) {
    return status;
  }

  // ABR metadata has no need of a content type since it's always local rather
  // than provided in an update package, so just use the default content type.
  std::unique_ptr<paver::PartitionClient> partition;
  if (zx_status_t status =
          partitioner->FindPartition(paver::PartitionSpec(paver::Partition::kAbrMeta), &partition);
      status != ZX_OK) {
    return status;
  }

  return PartitionClient::Create(std::move(partition), out);
}

extern "C" uint32_t AbrCrc32(const void* buf, size_t buf_size) {
  return crc32(0UL, reinterpret_cast<const uint8_t*>(buf), buf_size);
}

}  // namespace abr
