// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/lib/paver/abr-client.h"

#include <dirent.h>
#include <endian.h>
#include <fcntl.h>
#include <fidl/fuchsia.boot/cpp/wire.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <fidl/fuchsia.paver/cpp/wire.h>
#include <lib/abr/abr.h>
#include <lib/cksum.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/cpp/wire/wire_messaging.h>
#include <lib/fit/defer.h>
#include <lib/sys/component/cpp/service_client.h>
#include <stdio.h>
#include <string.h>
#include <zircon/errors.h>
#include <zircon/hw/gpt.h>
#include <zircon/status.h>

#include <string_view>

#include <fbl/algorithm.h>

#include "src/lib/uuid/uuid.h"
#include "src/storage/lib/paver/device-partitioner.h"
#include "src/storage/lib/paver/partition-client.h"
#include "src/storage/lib/paver/pave-logging.h"

namespace abr {

namespace partition = fuchsia_hardware_block_partition;
using fuchsia_paver::wire::Asset;
using fuchsia_paver::wire::Configuration;

zx::result<Configuration> CurrentSlotToConfiguration(std::string_view slot) {
  // Some bootloaders prefix slot with dash or underscore. We strip them for consistency.
  slot.remove_prefix(std::min(slot.find_first_not_of("_-"), slot.size()));
  if (slot.compare("a") == 0) {
    return zx::ok(Configuration::kA);
  } else if (slot.compare("b") == 0) {
    return zx::ok(Configuration::kB);
  } else if (slot.compare("r") == 0) {
    return zx::ok(Configuration::kRecovery);
  }
  ERROR("Invalid value `%.*s` found in zvb.current_slot!\n", static_cast<int>(slot.size()),
        slot.data());
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

bool FindPartitionLabelByGuid(const fbl::unique_fd& devfs_root, const uint8_t* guid,
                              std::string& out) {
  constexpr char kBlockDevPath[] = "class/block/";
  out.clear();
  fbl::unique_fd d_fd(openat(devfs_root.get(), kBlockDevPath, O_RDONLY));
  if (!d_fd) {
    ERROR("Cannot inspect block devices\n");
    return false;
  }

  DIR* d = fdopendir(d_fd.release());
  if (d == nullptr) {
    ERROR("Cannot inspect block devices\n");
    return false;
  }
  const auto closer = fit::defer([&]() { closedir(d); });

  struct dirent* de;
  while ((de = readdir(d)) != nullptr) {
    fbl::unique_fd fd(openat(dirfd(d), de->d_name, O_RDWR));
    if (!fd) {
      continue;
    }
    fdio_cpp::FdioCaller caller(std::move(fd));

    auto result = fidl::WireCall<partition::Partition>(caller.channel())->GetInstanceGuid();
    if (!result.ok()) {
      continue;
    }
    const auto& response = result.value();
    if (response.status != ZX_OK) {
      continue;
    }
    if (memcmp(response.guid->value.data_, guid, GPT_GUID_LEN) != 0) {
      continue;
    }

    auto result2 = fidl::WireCall<partition::Partition>(caller.channel())->GetName();
    if (!result2.ok()) {
      continue;
    }

    const auto& response2 = result2.value();
    if (response2.status != ZX_OK) {
      continue;
    }
    std::string_view name(response2.name.data(), response2.name.size());
    out.append(name);
    return true;
  }

  return false;
}

zx::result<Configuration> PartitionUuidToConfiguration(const fbl::unique_fd& devfs_root,
                                                       uuid::Uuid uuid) {
  std::string name;
  auto result = FindPartitionLabelByGuid(devfs_root, uuid.bytes(), name);
  if (!result) {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  // Partition label should be zircon-<slot> or zircon_<slot>. This is case insensitive.
  static const size_t kZirconLength = sizeof("zircon") - 1;  // no null terminator.
  // Partition must start with "zircon".
  if (strncasecmp(name.data(), "zircon", kZirconLength) != 0) {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  name.erase(0, kZirconLength);

  if (name[0] != '-' && name[0] != '_') {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  switch (name[1]) {
    case 'a':
    case 'A':
      return zx::ok(Configuration::kA);
    case 'b':
    case 'B':
      return zx::ok(Configuration::kB);
    case 'r':
    case 'R':
      return zx::ok(Configuration::kRecovery);
  }

  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::result<Configuration> QueryBootConfig(const fbl::unique_fd& devfs_root,
                                          fidl::UnownedClientEnd<fuchsia_io::Directory> svc_root) {
  auto client_end = component::ConnectAt<fuchsia_boot::Arguments>(svc_root);
  if (!client_end.is_ok()) {
    return client_end.take_error();
  }
  fidl::WireSyncClient client{std::move(*client_end)};
  std::array<fidl::StringView, 2> arguments{
      fidl::StringView{"zvb.current_slot"},
      fidl::StringView{"zvb.boot-partition-uuid"},
  };
  auto result = client->GetStrings(fidl::VectorView<fidl::StringView>::FromExternal(arguments));
  if (!result.ok()) {
    return zx::error(result.status());
  }

  const auto response = result.Unwrap();
  if (!response->values[0].is_null()) {
    return CurrentSlotToConfiguration(response->values[0].get());
  }
  if (!response->values[1].is_null()) {
    auto uuid = uuid::Uuid::FromString(response->values[1].get());
    if (uuid == std::nullopt) {
      return zx::error(ZX_ERR_NOT_SUPPORTED);
    }

    return PartitionUuidToConfiguration(devfs_root, uuid.value());
  }

  ERROR("Kernel cmdline param zvb.current_slot and zvb.boot-partition-uuid not found!\n");
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

namespace {

zx::result<> SupportsVerifiedBoot(const fbl::unique_fd& devfs_root,
                                  fidl::UnownedClientEnd<fuchsia_io::Directory> svc_root) {
  return zx::make_result(QueryBootConfig(devfs_root, svc_root).status_value());
}
}  // namespace

zx::result<std::unique_ptr<abr::Client>> AbrPartitionClient::Create(
    std::unique_ptr<paver::PartitionClient> partition) {
  auto status = partition->GetBlockSize();
  if (status.is_error()) {
    return status.take_error();
  }
  size_t block_size = status.value();

  zx::vmo vmo;
  if (auto status = zx::make_result(
          zx::vmo::create(fbl::round_up(block_size, zx_system_get_page_size()), 0, &vmo));
      status.is_error()) {
    return status.take_error();
  }

  if (auto status = partition->Read(vmo, block_size); status.is_error()) {
    return status.take_error();
  }

  return zx::ok(new AbrPartitionClient(std::move(partition), std::move(vmo), block_size));
}

zx::result<> AbrPartitionClient::Read(uint8_t* buffer, size_t size) {
  if (auto status = partition_->Read(vmo_, block_size_); status.is_error()) {
    return status.take_error();
  }
  if (auto status = zx::make_result(vmo_.read(buffer, 0, size)); status.is_error()) {
    return status.take_error();
  }
  return zx::ok();
}

zx::result<> AbrPartitionClient::Write(const uint8_t* buffer, size_t size) {
  if (auto status = zx::make_result(vmo_.write(buffer, 0, size)); status.is_error()) {
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

zx::result<std::unique_ptr<abr::Client>> ClientFactory::Create(
    fbl::unique_fd devfs_root, fidl::UnownedClientEnd<fuchsia_io::Directory> svc_root,
    std::shared_ptr<paver::Context> context) {
  if (auto status = SupportsVerifiedBoot(devfs_root, svc_root); status.is_error()) {
    return status.take_error();
  }

  for (auto& factory : *registered_factory_list()) {
    if (auto status = factory->New(devfs_root.duplicate(), svc_root, std::move(context));
        status.is_ok()) {
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

bool Client::ReadAbrMetadataCustom(void* context, AbrSlotData* a, AbrSlotData* b,
                                   uint8_t* one_shot_recovery) {
  if (auto res = static_cast<Client*>(context)->ReadCustom(a, b, one_shot_recovery);
      res.is_error()) {
    ERROR("Failed to read abr data from storage. %s\n", res.status_string());
    return false;
  }
  return true;
}

bool Client::WriteAbrMetadataCustom(void* context, const AbrSlotData* a, const AbrSlotData* b,
                                    uint8_t one_shot_recovery) {
  if (auto res = static_cast<Client*>(context)->WriteCustom(a, b, one_shot_recovery);
      res.is_error()) {
    ERROR("Failed to read abr data from storage. %s\n", res.status_string());
    return false;
  }
  return true;
}

zx::result<> Client::AbrResultToZxStatus(AbrResult status) {
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
