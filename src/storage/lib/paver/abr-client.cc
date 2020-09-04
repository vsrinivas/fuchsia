// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/lib/paver/abr-client.h"

#include <endian.h>
#include <fuchsia/boot/llcpp/fidl.h>
#include <lib/abr/abr.h>
#include <lib/cksum.h>
#include <lib/fdio/directory.h>
#include <stdio.h>
#include <string.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <string_view>

#include "src/storage/lib/paver/device-partitioner.h"
#include "src/storage/lib/paver/partition-client.h"
#include "src/storage/lib/paver/pave-logging.h"

namespace abr {

using ::llcpp::fuchsia::paver::Asset;
using ::llcpp::fuchsia::paver::Configuration;

zx::status<Configuration> QueryBootConfig(const zx::channel& svc_root) {
  zx::channel local, remote;
  if (auto status = zx::make_status(zx::channel::create(0, &local, &remote)); status.is_error()) {
    return status.take_error();
  }
  auto status = zx::make_status(fdio_service_connect_at(
      svc_root.get(), ::llcpp::fuchsia::boot::Arguments::Name, remote.release()));
  if (status.is_error()) {
    return status.take_error();
  }
  ::llcpp::fuchsia::boot::Arguments::SyncClient client(std::move(local));
  auto result = client.GetString(::fidl::StringView{"zvb.current_slot"});
  if (!result.ok()) {
    return zx::error(result.status());
  }

  const auto response = result.Unwrap();
  if (response->value.is_null()) {
    ERROR("Kernel cmdline param zvb.current_slot not found!\n");
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  auto slot = std::string_view{response->value.data(), response->value.size()};
  // Some bootloaders prefix slot with dash or underscore. We strip them for consistency.
  slot.remove_prefix(std::min(slot.find_first_not_of("_-"), slot.size()));
  if (slot.compare("a") == 0) {
    return zx::ok(Configuration::A);
  } else if (slot.compare("b") == 0) {
    return zx::ok(Configuration::B);
  } else if (slot.compare("r") == 0) {
    return zx::ok(Configuration::RECOVERY);
  }
  ERROR("Invalid value `%.*s` found in zvb.current_slot!\n", static_cast<int>(slot.size()),
        slot.data());
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

namespace {

zx::status<> SupportsVerifiedBoot(const zx::channel& svc_root) {
  return zx::make_status(QueryBootConfig(svc_root).status_value());
}
}  // namespace

zx::status<std::unique_ptr<abr::Client>> AbrPartitionClient::Create(
    std::unique_ptr<paver::PartitionClient> partition) {
  auto status = partition->GetBlockSize();
  if (status.is_error()) {
    return status.take_error();
  }
  size_t block_size = status.value();

  zx::vmo vmo;
  if (auto status =
          zx::make_status(zx::vmo::create(fbl::round_up(block_size, ZX_PAGE_SIZE), 0, &vmo));
      status.is_error()) {
    return status.take_error();
  }

  if (auto status = partition->Read(vmo, block_size); status.is_error()) {
    return status.take_error();
  }

  return zx::ok(new AbrPartitionClient(std::move(partition), std::move(vmo), block_size));
}

zx::status<> AbrPartitionClient::Read(uint8_t* buffer, size_t size) {
  if (auto status = partition_->Read(vmo_, block_size_); status.is_error()) {
    return status.take_error();
  }
  if (auto status = zx::make_status(vmo_.read(buffer, 0, size)); status.is_error()) {
    return status.take_error();
  }
  return zx::ok();
}

zx::status<> AbrPartitionClient::Write(const uint8_t* buffer, size_t size) {
  if (auto status = zx::make_status(vmo_.write(buffer, 0, size)); status.is_error()) {
    return status.take_error();
  }
  if (auto status = partition_->Write(vmo_, block_size_); status.is_error()) {
    return status.take_error();
  }
  return zx::ok();
}

std::vector<std::unique_ptr<ClientFactory>>* ClientFactory::registered_factory_list() {
  static std::vector<std::unique_ptr<ClientFactory>>* registered_factory_list = nullptr;
  if (registered_factory_list == nullptr) {
    registered_factory_list = new std::vector<std::unique_ptr<ClientFactory>>();
  }
  return registered_factory_list;
}

zx::status<std::unique_ptr<abr::Client>> ClientFactory::Create(
    fbl::unique_fd devfs_root, const zx::channel& svc_root,
    std::shared_ptr<paver::Context> context) {
  if (auto status = SupportsVerifiedBoot(svc_root); status.is_error()) {
    return status.take_error();
  }

  for (auto& factory : *registered_factory_list()) {
    if (auto status = factory->New(devfs_root.duplicate(), svc_root, context); status.is_ok()) {
      return status.take_value();
    }
  }

  return zx::error(ZX_ERR_NOT_FOUND);
}

void ClientFactory::Register(std::unique_ptr<ClientFactory> factory) {
  registered_factory_list()->push_back(std::move(factory));
}

bool Client::ReadAbrMetaData(void* context, size_t size, uint8_t* buffer) {
  if (auto res = static_cast<Client*>(context)->Read(buffer, size); res.is_error()) {
    ERROR("Failed to read abr data from storage. %s\n", res.status_string());
    return false;
  }
  return true;
}

bool Client::WriteAbrMetaData(void* context, const uint8_t* buffer, size_t size) {
  if (auto res = static_cast<Client*>(context)->Write(buffer, size); res.is_error()) {
    ERROR("Failed to write abr data to storage. %s\n", res.status_string());
    return false;
  }
  return true;
}

zx::status<> Client::AbrResultToZxStatus(AbrResult status) {
  switch (status) {
    case kAbrResultOk:
      return zx::ok();
    case kAbrResultErrorIo:
      return zx::error(ZX_ERR_IO);
    case kAbrResultErrorInvalidData:
      return zx::error(ZX_ERR_INVALID_ARGS);
    case kAbrResultErrorUnsupportedVersion:
      return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
  ERROR("Unknown Abr result code %d!\n", status);
  return zx::error(ZX_ERR_INTERNAL);
}

extern "C" uint32_t AbrCrc32(const void* buf, size_t buf_size) {
  return crc32(0UL, reinterpret_cast<const uint8_t*>(buf), buf_size);
}

}  // namespace abr
