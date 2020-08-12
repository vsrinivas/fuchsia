// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/lib/paver/astro.h"

#include <fuchsia/boot/llcpp/fidl.h>
#include <lib/fdio/directory.h>

#include <gpt/gpt.h>
#include <soc/aml-common/aml-guid.h>

#include "src/lib/uuid/uuid.h"
#include "src/storage/lib/paver/pave-logging.h"
#include "src/storage/lib/paver/sysconfig.h"
#include "src/storage/lib/paver/utils.h"

namespace paver {

namespace {

using uuid::Uuid;

std::optional<::llcpp::fuchsia::boot::Arguments::SyncClient> OpenBootArgumentClient(
    const zx::channel& svc_root) {
  if (!svc_root.is_valid()) {
    return {};
  }

  zx::channel local, remote;
  if (zx_status_t status = zx::channel::create(0, &local, &remote); status != ZX_OK) {
    ERROR("Failed to create channel. \n");
    return {};
  }

  auto status = fdio_service_connect_at(svc_root.get(), ::llcpp::fuchsia::boot::Arguments::Name,
                                        remote.release());
  if (status != ZX_OK) {
    ERROR("Failed to connect to boot::Arguments service.\n");
    return {};
  }

  return {::llcpp::fuchsia::boot::Arguments::SyncClient(std::move(local))};
}

bool GetBool(::llcpp::fuchsia::boot::Arguments::SyncClient& client, ::fidl::StringView key,
             bool default_on_missing_or_failure) {
  auto key_data = key.data();
  auto result = client.GetBool(std::move(key), default_on_missing_or_failure);
  if (!result.ok()) {
    ERROR("Failed to get boolean argument %s. Default to %d.\n", key_data,
          default_on_missing_or_failure);
    return default_on_missing_or_failure;
  }
  return result->value;
}

}  // namespace

bool AstroPartitioner::CanSafelyUpdateLayout(std::shared_ptr<Context> context) {
  // Condition: one successful slot + one unbootable slot
  // Once the layout is updated, it is dangerous to roll back to an older version of system
  // that doesn't have the new logic. Therefore, here we require the A/B state to be in the
  // above state, where it is impossible to roll back to older version.
  std::unique_ptr<PartitionClient> partition_client =
      std::make_unique<AstroSysconfigPartitionClientBuffered>(
          context, sysconfig::SyncClient::PartitionType::kABRMetadata);

  auto status_or_client = abr::AbrPartitionClient::Create(std::move(partition_client));
  if (status_or_client.is_error()) {
    LOG("Failed to create abr-client. Conservatively consider not safe to update layout. %s\n",
        status_or_client.status_string());
    return false;
  }
  std::unique_ptr<abr::Client>& abr_client = status_or_client.value();

  auto status_or_a = abr_client->GetSlotInfo(kAbrSlotIndexA);
  if (status_or_a.is_error()) {
    LOG("Failed to get info for slot A. Conservatively consider not safe to update layout. %s\n",
        status_or_a.status_string());
    return false;
  }
  AbrSlotInfo& slot_a = status_or_a.value();

  auto status_or_b = abr_client->GetSlotInfo(kAbrSlotIndexB);
  if (status_or_b.is_error()) {
    LOG("Failed to get info for slot B. Conservatively consider not safe to update layout. %s\n",
        status_or_b.status_string());
    return false;
  }
  AbrSlotInfo& slot_b = status_or_b.value();

  if (!slot_a.is_marked_successful && !slot_b.is_marked_successful) {
    LOG("No slot is marked successful. Not updating layout.\n")
    return false;
  }

  if (slot_a.is_bootable && slot_b.is_bootable) {
    LOG("The other slot is not marked unbootable. Not updating layout.\n")
    return false;
  }

  return true;
}

zx::status<> AstroPartitioner::InitializeContext(const fbl::unique_fd& devfs_root,
                                                 AbrWearLevelingOption abr_wear_leveling_opt,
                                                 std::shared_ptr<Context> context) {
  return context->Initialize<AstroPartitionerContext>(
      [&]() -> zx::status<std::unique_ptr<AstroPartitionerContext>> {
        std::optional<sysconfig::SyncClient> client;
        if (auto status = zx::make_status(sysconfig::SyncClient::Create(devfs_root, &client));
            status.is_error()) {
          ERROR("Failed to initialize context. %s\n", status.status_string());
          return status.take_error();
        }

        std::unique_ptr<::sysconfig::SyncClientBuffered> sysconfig_client;
        if (abr_wear_leveling_opt == AbrWearLevelingOption::OFF) {
          sysconfig_client = std::make_unique<::sysconfig::SyncClientBuffered>(*std::move(client));
          LOG("Using SyncClientBuffered\n");
        } else if (abr_wear_leveling_opt == AbrWearLevelingOption::ON) {
          sysconfig_client =
              std::make_unique<::sysconfig::SyncClientAbrWearLeveling>(*std::move(client));
          LOG("Using SyncClientAbrWearLeveling\n");
        } else {
          ZX_ASSERT_MSG(false, "Unknown AbrWearLevelingOption %d\n",
                        static_cast<int>(abr_wear_leveling_opt));
        }

        return zx::ok(new AstroPartitionerContext(std::move(sysconfig_client)));
      });
}

zx::status<std::unique_ptr<DevicePartitioner>> AstroPartitioner::Initialize(
    fbl::unique_fd devfs_root, const zx::channel& svc_root, std::shared_ptr<Context> context) {
  auto boot_arg_client = OpenBootArgumentClient(svc_root);
  zx::status<> status = IsBoard(devfs_root, "astro");
  if (status.is_error()) {
    return status.take_error();
  }

  // TODO(fxbug.dev/47505): Removed this condition and always enabled buffered client when
  // http://fxb/40952 is fixed.
  bool enabled_buffered_client =
      boot_arg_client && GetBool(*boot_arg_client, "astro.sysconfig.buffered-client", false);

  if (enabled_buffered_client) {
    // Enable abr wear-leveling only when we see an explicitly defined boot argument
    // "astro.sysconfig.abr-wear-leveling".
    // TODO(fxbug.dev/47505): Find a proper place to document the parameter.
    AbrWearLevelingOption option =
        boot_arg_client && GetBool(*boot_arg_client, "astro.sysconfig.abr-wear-leveling", false)
            ? AbrWearLevelingOption::ON
            : AbrWearLevelingOption::OFF;

    if (auto status = InitializeContext(devfs_root, option, context); status.is_error()) {
      ERROR("Failed to initialize context. %s\n", status.status_string());
      return status.take_error();
    }

    // CanSafelyUpdateLayout internally acquires the context.lock_. Thus we don't put it in
    // context.Call() or InitializeContext to avoid dead lock.
    if (option == AbrWearLevelingOption::ON && CanSafelyUpdateLayout(context)) {
      if (auto status = context->Call<AstroPartitionerContext>([](auto* ctx) {
            return zx::make_status(ctx->client_->UpdateLayout(
                ::sysconfig::SyncClientAbrWearLeveling::GetAbrWearLevelingSupportedLayout()));
          });
          status.is_error()) {
        return status.take_error();
      }
    }
  }

  LOG("Successfully initialized AstroPartitioner Device Partitioner\n");
  std::unique_ptr<SkipBlockDevicePartitioner> skip_block(
      new SkipBlockDevicePartitioner(std::move(devfs_root)));

  return zx::ok(
      new AstroPartitioner(std::move(skip_block), enabled_buffered_client ? context : nullptr));
}

// Astro bootloader types:
//
// -- default --
// The TPL bootloader image.
//
// Initially we only supported updating the TPL bootloader, so for backwards
// compatibility this must be the default type.
//
// -- "bl2" --
// The BL2 bootloader image.
//
// It's easier to provide the two images separately since on Astro they live
// in different partitions, rather than having to split a combined image.
bool AstroPartitioner::SupportsPartition(const PartitionSpec& spec) const {
  const PartitionSpec supported_specs[] = {PartitionSpec(paver::Partition::kBootloader),
                                           PartitionSpec(paver::Partition::kBootloader, "bl2"),
                                           PartitionSpec(paver::Partition::kZirconA),
                                           PartitionSpec(paver::Partition::kZirconB),
                                           PartitionSpec(paver::Partition::kZirconR),
                                           PartitionSpec(paver::Partition::kVbMetaA),
                                           PartitionSpec(paver::Partition::kVbMetaB),
                                           PartitionSpec(paver::Partition::kVbMetaR),
                                           PartitionSpec(paver::Partition::kAbrMeta),
                                           PartitionSpec(paver::Partition::kFuchsiaVolumeManager)};

  for (const auto& supported : supported_specs) {
    if (SpecMatches(spec, supported)) {
      return true;
    }
  }

  return false;
}

zx::status<std::unique_ptr<PartitionClient>> AstroPartitioner::AddPartition(
    const PartitionSpec& spec) const {
  ERROR("Cannot add partitions to an astro.\n");
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::status<std::unique_ptr<PartitionClient>> AstroPartitioner::FindPartition(
    const PartitionSpec& spec) const {
  if (!SupportsPartition(spec)) {
    ERROR("Unsupported partition %s\n", spec.ToString().c_str());
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  switch (spec.partition) {
    case Partition::kBootloader: {
      if (spec.content_type.empty()) {
        // Default type = TPL.
        Uuid tpl_type = GUID_BOOTLOADER_VALUE;
        return skip_block_->FindPartition(tpl_type);
      } else if (spec.content_type == "bl2") {
        if (auto status = skip_block_->FindPartition(GUID_BL2_VALUE); status.is_error()) {
          return status.take_error();
        } else {
          std::unique_ptr<PartitionClient>& bl2_skip_block = status.value();

          // Upgrade this into a more specialized partition client for custom
          // handling required by BL2.
          return zx::ok(new Bl2PartitionClient(bl2_skip_block->GetChannel()));
        }
      }
      // If we get here, we must have added another type to SupportsPartition()
      // without actually implementing it.
      ERROR("Unimplemeneted partition '%s'\n", spec.ToString().c_str());
      return zx::error(ZX_ERR_INTERNAL);
    }
    case Partition::kZirconA:
      return skip_block_->FindPartition(GUID_ZIRCON_A_VALUE);

    case Partition::kZirconB:
      return skip_block_->FindPartition(GUID_ZIRCON_B_VALUE);

    case Partition::kZirconR:
      return skip_block_->FindPartition(GUID_ZIRCON_R_VALUE);

    case Partition::kVbMetaA:
    case Partition::kVbMetaB:
    case Partition::kVbMetaR:
    case Partition::kAbrMeta: {
      const auto type = [&]() {
        switch (spec.partition) {
          case Partition::kVbMetaA:
            return sysconfig::SyncClient::PartitionType::kVerifiedBootMetadataA;
          case Partition::kVbMetaB:
            return sysconfig::SyncClient::PartitionType::kVerifiedBootMetadataB;
          case Partition::kVbMetaR:
            return sysconfig::SyncClient::PartitionType::kVerifiedBootMetadataR;
          case Partition::kAbrMeta:
            return sysconfig::SyncClient::PartitionType::kABRMetadata;
          default:
            break;
        }
        ZX_ASSERT(false);
      }();
      // TODO(fxbug.dev/47505): Remove the following check and always use buffered client when
      // http://fxb/40952 is fixed.
      if (context_) {
        return zx::ok(new AstroSysconfigPartitionClientBuffered(context_, type));
      } else {
        std::optional<sysconfig::SyncClient> client;
        zx_status_t status = sysconfig::SyncClient::Create(skip_block_->devfs_root(), &client);
        if (status != ZX_OK) {
          return zx::error(status);
        }
        return zx::ok(new SysconfigPartitionClient(*std::move(client), type));
      }
    }
    case Partition::kFuchsiaVolumeManager: {
      return skip_block_->FindFvmPartition();
    }
    default:
      ERROR("partition_type is invalid!\n");
      return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
}

zx::status<> AstroPartitioner::Flush() const {
  return context_ ? context_->Call<AstroPartitionerContext>(
                        [&](auto* ctx) { return zx::make_status(ctx->client_->Flush()); })
                  : zx::ok();
}

zx::status<> AstroPartitioner::WipeFvm() const { return skip_block_->WipeFvm(); }

zx::status<> AstroPartitioner::InitPartitionTables() const {
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::status<> AstroPartitioner::WipePartitionTables() const {
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::status<> AstroPartitioner::ValidatePayload(const PartitionSpec& spec,
                                               fbl::Span<const uint8_t> data) const {
  if (!SupportsPartition(spec)) {
    ERROR("Unsupported partition %s\n", spec.ToString().c_str());
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  return zx::ok();
}

zx::status<std::unique_ptr<DevicePartitioner>> AstroPartitionerFactory::New(
    fbl::unique_fd devfs_root, const zx::channel& svc_root, Arch arch,
    std::shared_ptr<Context> context, const fbl::unique_fd& block_device) {
  return AstroPartitioner::Initialize(std::move(devfs_root), svc_root, context);
}

zx::status<std::unique_ptr<abr::Client>> AstroAbrClientFactory::New(
    fbl::unique_fd devfs_root, const zx::channel& svc_root,
    std::shared_ptr<paver::Context> context) {
  auto status = AstroPartitioner::Initialize(std::move(devfs_root), svc_root, context);
  if (status.is_error()) {
    return status.take_error();
  }
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();

  // ABR metadata has no need of a content type since it's always local rather
  // than provided in an update package, so just use the default content type.
  auto status_or_part =
      partitioner->FindPartition(paver::PartitionSpec(paver::Partition::kAbrMeta));
  if (status_or_part.is_error()) {
    return status_or_part.take_error();
  }

  return abr::AbrPartitionClient::Create(std::move(status_or_part.value()));
}

zx::status<size_t> AstroSysconfigPartitionClientBuffered::GetBlockSize() {
  return context_->Call<AstroPartitionerContext, size_t>([&](auto* ctx) -> zx::status<size_t> {
    size_t size;
    auto status = zx::make_status(ctx->client_->GetPartitionSize(partition_, &size));
    if (status.is_error()) {
      return status.take_error();
    }
    return zx::ok(size);
  });
}

zx::status<size_t> AstroSysconfigPartitionClientBuffered::GetPartitionSize() {
  return context_->Call<AstroPartitionerContext, size_t>([&](auto* ctx) -> zx::status<size_t> {
    size_t size;
    auto status = zx::make_status(ctx->client_->GetPartitionSize(partition_, &size));
    if (status.is_error()) {
      return status.take_error();
    }
    return zx::ok(size);
  });
}

zx::status<> AstroSysconfigPartitionClientBuffered::Read(const zx::vmo& vmo, size_t size) {
  return context_->Call<AstroPartitionerContext>(
      [&](auto* ctx) { return zx::make_status(ctx->client_->ReadPartition(partition_, vmo, 0)); });
}

zx::status<> AstroSysconfigPartitionClientBuffered::Write(const zx::vmo& vmo, size_t size) {
  return context_->Call<AstroPartitionerContext>([&](auto* ctx) -> zx::status<> {
    size_t partition_size;
    auto status = zx::make_status(ctx->client_->GetPartitionSize(partition_, &partition_size));
    if (status.is_error()) {
      return status.take_error();
    }
    if (size != partition_size) {
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
    return zx::make_status(ctx->client_->WritePartition(partition_, vmo, 0));
  });
}

zx::status<> AstroSysconfigPartitionClientBuffered::Trim() {
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::status<> AstroSysconfigPartitionClientBuffered::Flush() {
  return context_->Call<AstroPartitionerContext>(
      [&](auto* ctx) { return zx::make_status(ctx->client_->Flush()); });
}

zx::channel AstroSysconfigPartitionClientBuffered::GetChannel() { return {}; }

fbl::unique_fd AstroSysconfigPartitionClientBuffered::block_fd() { return fbl::unique_fd(); }

zx::status<size_t> Bl2PartitionClient::GetBlockSize() {
  // Technically this is incorrect, but we deal with alignment so this is okay.
  return zx::ok(kBl2Size);
}

zx::status<size_t> Bl2PartitionClient::GetPartitionSize() { return zx::ok(kBl2Size); }

zx::status<> Bl2PartitionClient::Read(const zx::vmo& vmo, size_t size) {
  // Create a vmo to read a full block.
  auto status = SkipBlockPartitionClient::GetBlockSize();
  if (status.is_error()) {
    return status.take_error();
  }
  const size_t block_size = status.value();

  zx::vmo full;
  if (auto status = zx::make_status(zx::vmo::create(block_size, 0, &full)); status.is_error()) {
    return status.take_error();
  }

  if (auto status = SkipBlockPartitionClient::Read(full, block_size); status.is_error()) {
    return status.take_error();
  }

  // Copy correct region (pages 1 - 65) to the VMO.
  auto buffer = std::make_unique<uint8_t[]>(block_size);
  if (auto status = zx::make_status(full.read(buffer.get(), kNandPageSize, kBl2Size));
      status.is_error()) {
    return status.take_error();
  }
  if (auto status = zx::make_status(vmo.write(buffer.get(), 0, kBl2Size)); status.is_error()) {
    return status.take_error();
  }

  return zx::ok();
}

zx::status<> Bl2PartitionClient::Write(const zx::vmo& vmo, size_t size) {
  if (size != kBl2Size) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  return WriteBytes(vmo, kNandPageSize, kBl2Size);
}

}  // namespace paver
