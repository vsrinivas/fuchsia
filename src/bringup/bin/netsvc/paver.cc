// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "paver.h"

#include <fcntl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/sysconfig/sync-client.h>
#include <lib/zx/clock.h>
#include <stdio.h>
#include <zircon/boot/netboot.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <algorithm>
#include <string>
#include <string_view>

#include <fbl/auto_call.h>

#include "lib/async-loop/loop.h"
#include "lib/fdio/cpp/caller.h"
#include "payload-streamer.h"
#include "zircon/errors.h"

namespace netsvc {
namespace {

zx_status_t ClearSysconfig(const fbl::unique_fd& devfs_root) {
  std::optional<sysconfig::SyncClient> client;
  zx_status_t status = sysconfig::SyncClient::Create(devfs_root, &client);
  if (status == ZX_ERR_NOT_SUPPORTED) {
    // We only clear sysconfig on devices with sysconfig partition.
    return ZX_OK;
  } else if (status != ZX_OK) {
    fprintf(stderr, "netsvc: Failed to create sysconfig SyncClient.\n");
    return status;
  }
  constexpr auto kPartition = sysconfig::SyncClient::PartitionType::kSysconfig;
  size_t size;
  if (auto status_get_size = client->GetPartitionSize(kPartition, &size) != ZX_OK) {
    fprintf(stderr, "Failed to get partition size. %s\n", zx_status_get_string(status_get_size));
    return status_get_size;
  }

  // We assume the vmo is zero initialized.
  zx::vmo vmo;
  status = zx::vmo::create(fbl::round_up(size, ZX_PAGE_SIZE), 0, &vmo);
  if (status != ZX_OK) {
    fprintf(stderr, "netsvc: Failed to create vmo.\n");
    return status;
  }

  status = client->WritePartition(kPartition, vmo, 0);
  if (status != ZX_OK) {
    fprintf(stderr, "netsvc: Failed to write to sysconfig partition.\n");
    return status;
  }

  return ZX_OK;
}

}  // namespace

Paver* Paver::Get() {
  static Paver* instance_ = nullptr;
  if (instance_ == nullptr) {
    zx::channel local, remote;
    auto status = zx::channel::create(0, &local, &remote);
    if (status != ZX_OK) {
      return nullptr;
    }
    status = fdio_service_connect("/svc", remote.release());
    if (status != ZX_OK) {
      return nullptr;
    }
    fbl::unique_fd devfs_root(open("/dev", O_RDONLY));
    if (!devfs_root) {
      return nullptr;
    }

    instance_ = new Paver(std::move(local), std::move(devfs_root));
  }
  return instance_;
}

bool Paver::InProgress() { return in_progress_.load(); }
zx_status_t Paver::exit_code() { return exit_code_.load(); }
void Paver::reset_exit_code() { exit_code_.store(ZX_OK); }

int Paver::StreamBuffer() {
  zx::time last_reported = zx::clock::get_monotonic();
  size_t decommitted_offset = 0;
  int result = 0;
  auto callback = [this, &last_reported, &decommitted_offset, &result](
                      void* buf, size_t read_offset, size_t size, size_t* actual) {
    if (read_offset >= size_) {
      *actual = 0;
      return ZX_OK;
    }
    sync_completion_reset(&data_ready_);
    size_t write_offset = write_offset_.load();
    while (write_offset == read_offset) {
      // Wait for more data to be written -- we are allowed up to 3 tftp timeouts before
      // a connection is dropped, so we should wait at least that long before giving up.
      auto status = sync_completion_wait(&data_ready_, timeout_.get());
      if (status != ZX_OK) {
        printf("netsvc: 1 timed out while waiting for data in paver-copy thread\n");
        exit_code_.store(status);
        result = TFTP_ERR_TIMED_OUT;
        return ZX_ERR_TIMED_OUT;
      }
      sync_completion_reset(&data_ready_);
      write_offset = write_offset_.load();
    };
    size = std::min(size, write_offset - read_offset);
    memcpy(buf, buffer() + read_offset, size);
    *actual = size;

    // Best effort try to decommit pages we have already copied. This will prevent us from
    // running out of memory.
    ZX_ASSERT(read_offset + size > decommitted_offset);
    const size_t decommit_size =
        fbl::round_down(read_offset + size - decommitted_offset, ZX_PAGE_SIZE);
    // TODO(surajmalhotra): Tune this in case we decommit too aggresively.
    if (decommit_size > 0) {
      if (auto status = buffer_mapper_.vmo().op_range(ZX_VMO_OP_DECOMMIT, decommitted_offset,
                                                      decommit_size, nullptr, 0);
          status != ZX_OK) {
        printf("netsvc: Failed to decommit offset 0x%zx with size: 0x%zx: %s\n", decommitted_offset,
               decommit_size, zx_status_get_string(status));
      }
      decommitted_offset += decommit_size;
    }

    zx::time curr_time = zx::clock::get_monotonic();
    if (curr_time - last_reported >= zx::sec(1)) {
      float complete = (static_cast<float>(read_offset) / static_cast<float>(size_)) * 100.f;
      printf("netsvc: paver write progress %0.1f%%\n", complete);
      last_reported = curr_time;
    }
    return ZX_OK;
  };

  fbl::AutoCall cleanup([this, &result]() {
    unsigned int refcount = std::atomic_fetch_sub(&buf_refcount_, 1u);
    if (refcount == 1) {
      buffer_mapper_.Reset();
    }

    paver_svc_.reset();

    if (result != 0) {
      printf("netsvc: copy exited prematurely (%d): expect paver errors\n", result);
    }

    in_progress_.store(false);
  });

  zx::channel data_sink, remote;
  auto status = zx::channel::create(0, &data_sink, &remote);
  if (status != ZX_OK) {
    fprintf(stderr, "netsvc: unable to create channel\n");
    exit_code_.store(status);
    return 0;
  }

  auto res = paver_svc_->FindDataSink(std::move(remote));
  status = res.status();
  if (status != ZX_OK) {
    fprintf(stderr, "netsvc: unable to find data sink\n");
    exit_code_.store(status);
    return 0;
  }

  zx::channel client, server;
  status = zx::channel::create(0, &client, &server);
  if (status != ZX_OK) {
    fprintf(stderr, "netsvc: unable to create channel\n");
    exit_code_.store(status);
    return 0;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  PayloadStreamer streamer(std::move(server), std::move(callback));
  loop.StartThread("payload-streamer");

  // Blocks untils paving is complete.
  auto res2 = ::llcpp::fuchsia::paver::DataSink::Call::WriteVolumes(zx::unowned(data_sink),
                                                                    std::move(client));
  status = res2.status() == ZX_OK ? res2.value().status : res2.status();

  exit_code_.store(status);
  return 0;
}

zx_status_t Paver::WriteAsset(::llcpp::fuchsia::paver::DataSink::SyncClient data_sink,
                              ::llcpp::fuchsia::mem::Buffer buffer) {
  zx::channel boot_manager_chan, remote;
  auto status = zx::channel::create(0, &boot_manager_chan, &remote);
  if (status != ZX_OK) {
    fprintf(stderr, "netsvc: unable to create channel\n");
    exit_code_.store(status);
    return 0;
  }

  auto res2 = paver_svc_->FindBootManager(std::move(remote));
  status = res2.status();
  if (status != ZX_OK) {
    fprintf(stderr, "netsvc: unable to find boot manager\n");
    exit_code_.store(status);
    return 0;
  }
  std::optional<::llcpp::fuchsia::paver::BootManager::SyncClient> boot_manager;
  boot_manager.emplace(std::move(boot_manager_chan));

  // First find out whether or not ABR is supported.
  {
    auto result = boot_manager->QueryActiveConfiguration();
    if (result.status() != ZX_OK) {
      boot_manager.reset();
    }
  }
  // Make sure to mark the configuration we are about to pave as no longer bootable.
  if (boot_manager && configuration_ != ::llcpp::fuchsia::paver::Configuration::RECOVERY) {
    auto result = boot_manager->SetConfigurationUnbootable(configuration_);
    auto status = result.ok() ? result->status : result.status();
    if (status != ZX_OK) {
      fprintf(stderr, "netsvc: Unable to set configuration as unbootable.\n");
      return status;
    }
  }
  {
    auto result = data_sink.WriteAsset(configuration_, asset_, std::move(buffer));
    auto status = result.ok() ? result->status : result.status();
    if (status != ZX_OK) {
      fprintf(stderr, "netsvc: Unable to write asset.\n");
      return status;
    }
  }
  // Set configuration A/B as default.
  // We assume that verified boot metadata asset will only be written after the kernel asset.
  if (!boot_manager || configuration_ == ::llcpp::fuchsia::paver::Configuration::RECOVERY ||
      asset_ != ::llcpp::fuchsia::paver::Asset::VERIFIED_BOOT_METADATA) {
    if (boot_manager) {
      auto res = boot_manager->Flush();
      auto status_sync = res.ok() ? res->status : res.status();
      if (status_sync != ZX_OK) {
        fprintf(stderr, "netsvc: failed to sync A/B/R configuration. %s\n",
                zx_status_get_string(status_sync));
        return status_sync;
      }
    }
    return ZX_OK;
  }
  {
    auto result = boot_manager->SetConfigurationActive(configuration_);
    auto status = result.ok() ? result->status : result.status();
    if (status != ZX_OK) {
      fprintf(stderr, "netsvc: Unable to set configuration as active.\n");
      return status;
    }
  }
  {
    auto opposite = configuration_ == ::llcpp::fuchsia::paver::Configuration::A
                        ? ::llcpp::fuchsia::paver::Configuration::B
                        : ::llcpp::fuchsia::paver::Configuration::A;

    auto result = boot_manager->SetConfigurationUnbootable(opposite);
    auto status = result.ok() ? result->status : result.status();
    if (status != ZX_OK) {
      fprintf(stderr, "netsvc: Unable to set opposite configuration as unbootable.\n");
      return status;
    }
  }

  // TODO(fxbug.dev/47505): The following two syncs are called everytime WriteAsset is called, which
  // is not optimal for reducing NAND PE cycles. Ideally, we want to sync when all assets, A/B
  // configuration have been written to buffer. Find a safe time and place for sync.
  {
    auto res = data_sink.Flush();
    auto status = res.ok() ? res->status : res.status();
    if (status != ZX_OK) {
      fprintf(stderr, "netsvc: failed to flush data_sink. %s\n", zx_status_get_string(status));
      return status;
    }
  }

  if (boot_manager) {
    auto res = boot_manager->Flush();
    auto status = res.ok() ? res->status : res.status();
    if (status != ZX_OK) {
      fprintf(stderr, "netsvc: failed to flush A/B/R configuration. %s\n",
              zx_status_get_string(status));
      return status;
    }
  }

  return ClearSysconfig(devfs_root_);
}

zx_status_t Paver::OpenDataSink(
    ::llcpp::fuchsia::mem::Buffer buffer,
    std::optional<::llcpp::fuchsia::paver::DynamicDataSink::SyncClient>* data_sink) {
  modify_partition_table_info_t partition_info = {};
  auto status = buffer.vmo.read(&partition_info, 0, sizeof(partition_info));
  if (status != ZX_OK) {
    fprintf(stderr, "netsvc: Unable to read from vmo\n");
    return status;
  }
  if (partition_info.block_device_path[ZX_MAX_NAME_LEN] != '\0') {
    fprintf(stderr, "netsvc: Invalid block device path specified\n");
    return ZX_ERR_INVALID_ARGS;
  }

  constexpr char kDevfsPrefix[] = "/dev/";
  if (strncmp(kDevfsPrefix, partition_info.block_device_path, strlen(kDevfsPrefix)) != 0) {
    fprintf(stderr, "netsvc: Invalid block device path specified\n");
    return ZX_ERR_INVALID_ARGS;
  }

  zx::channel block_dev_chan, remote;
  status = zx::channel::create(0, &block_dev_chan, &remote);
  if (status != ZX_OK) {
    fprintf(stderr, "netsvc: unable to create channel\n");
    return status;
  }

  fdio_cpp::UnownedFdioCaller caller(devfs_root_.get());

  status = fdio_service_connect_at(caller.borrow_channel(), &partition_info.block_device_path[5],
                                   remote.release());
  if (status != ZX_OK) {
    fprintf(stderr, "netsvc: Unable to open %s.\n", partition_info.block_device_path);
    return status;
  }

  zx::channel data_sink_chan;
  status = zx::channel::create(0, &data_sink_chan, &remote);
  if (status != ZX_OK) {
    fprintf(stderr, "netsvc: unable to create channel.\n");
    return status;
  }

  auto res = paver_svc_->UseBlockDevice(std::move(block_dev_chan), std::move(remote));
  status = res.status();
  if (status != ZX_OK) {
    fprintf(stderr, "netsvc: unable to use block device.\n");
    return status;
  }

  data_sink->emplace(std::move(data_sink_chan));
  return ZX_OK;
}

zx_status_t Paver::InitPartitionTables(::llcpp::fuchsia::mem::Buffer buffer) {
  std::optional<::llcpp::fuchsia::paver::DynamicDataSink::SyncClient> data_sink;
  auto status = OpenDataSink(std::move(buffer), &data_sink);
  if (status != ZX_OK) {
    fprintf(stderr, "netsvc: Unable to open data sink.\n");
    return status;
  }

  auto result = data_sink->InitializePartitionTables();
  status = result.ok() ? result->status : result.status();
  if (status != ZX_OK) {
    fprintf(stderr, "netsvc: Unable to initialize partition tables.\n");
    return status;
  }
  return ZX_OK;
}

zx_status_t Paver::WipePartitionTables(::llcpp::fuchsia::mem::Buffer buffer) {
  std::optional<::llcpp::fuchsia::paver::DynamicDataSink::SyncClient> data_sink;
  auto status = OpenDataSink(std::move(buffer), &data_sink);
  if (status != ZX_OK) {
    fprintf(stderr, "netsvc: Unable to open data sink.\n");
    return status;
  }

  auto result = data_sink->WipePartitionTables();
  status = result.ok() ? result->status : result.status();
  if (status != ZX_OK) {
    fprintf(stderr, "netsvc: Unable to wipe partition tables.\n");
    return status;
  }
  return ZX_OK;
}

int Paver::MonitorBuffer() {
  int result = TFTP_NO_ERROR;

  fbl::AutoCall cleanup([this, &result]() {
    unsigned int refcount = std::atomic_fetch_sub(&buf_refcount_, 1u);
    if (refcount == 1) {
      buffer_mapper_.Reset();
    }

    paver_svc_.reset();

    if (result != 0) {
      printf("netsvc: copy exited prematurely (%d): expect paver errors\n", result);
    }

    in_progress_.store(false);
  });

  size_t write_ndx = 0;
  do {
    // Wait for more data to be written -- we are allowed up to 3 tftp timeouts before
    // a connection is dropped, so we should wait at least that long before giving up.
    auto status = sync_completion_wait(&data_ready_, timeout_.get());
    if (status != ZX_OK) {
      printf("netsvc: 2 timed out while waiting for data in paver-copy thread\n");
      exit_code_.store(status);
      result = TFTP_ERR_TIMED_OUT;
      return result;
    }
    sync_completion_reset(&data_ready_);
    write_ndx = write_offset_.load();
  } while (write_ndx < size_);

  zx::vmo dup;
  auto status = buffer_mapper_.vmo().duplicate(ZX_RIGHT_SAME_RIGHTS, &dup);
  if (status != ZX_OK) {
    exit_code_.store(status);
    return 0;
  }

  ::llcpp::fuchsia::mem::Buffer buffer = {
      .vmo = std::move(dup),
      .size = buffer_mapper_.size(),
  };

  // We need to open a specific data sink rather than find the default for partition table
  // management commands.
  switch (command_) {
    case Command::kInitPartitionTables:
      status = InitPartitionTables(std::move(buffer));
      exit_code_.store(status);
      return 0;
    case Command::kWipePartitionTables:
      status = WipePartitionTables(std::move(buffer));
      exit_code_.store(status);
      return 0;
    default:
      break;
  };

  zx::channel remote, data_sink_chan;
  status = zx::channel::create(0, &data_sink_chan, &remote);
  if (status != ZX_OK) {
    fprintf(stderr, "netsvc: unable to create channel\n");
    exit_code_.store(status);
    return 0;
  }

  auto res = paver_svc_->FindDataSink(std::move(remote));
  status = res.status();
  if (status != ZX_OK) {
    fprintf(stderr, "netsvc: unable to find data sink\n");
    exit_code_.store(status);
    return 0;
  }
  ::llcpp::fuchsia::paver::DataSink::SyncClient data_sink(std::move(data_sink_chan));

  // Blocks untils paving is complete.
  switch (command_) {
    case Command::kDataFile: {
      auto res = data_sink.WriteDataFile(fidl::StringView(path_, strlen(path_)), std::move(buffer));
      status = res.status() == ZX_OK ? res.value().status : res.status();
      break;
    }
    case Command::kFirmware: {
      auto res = data_sink.WriteFirmware(fidl::StringView(firmware_type_, strlen(firmware_type_)),
                                         std::move(buffer));
      if (!res.ok()) {
        status = res.status();
      } else if (res->result.is_status()) {
        status = res->result.status();
      } else if (res->result.is_unsupported_type()) {
        // Log a message but just skip this, we want to keep going so that we
        // can add new firmware types in the future without breaking older
        // paver versions.
        printf("netsvc: skipping unsupported firmware type '%s'\n", firmware_type_);
        status = ZX_OK;
      } else {
        // We must have added another union field but forgot to update this code.
        fprintf(stderr, "netsvc: unknown WriteFirmware result\n");
        status = ZX_ERR_INTERNAL;
      }
      break;
    }
    case Command::kAsset:
      status = WriteAsset(std::move(data_sink), std::move(buffer));
      break;
    default:
      result = TFTP_ERR_INTERNAL;
      status = ZX_ERR_INTERNAL;
      break;
  }
  exit_code_.store(status);

  return 0;
}

namespace {

// If |string| starts with |prefix|, returns |string| with |prefix| removed.
// Otherwise, returns std::nullopt.
std::optional<std::string_view> WithoutPrefix(std::string_view string, std::string_view prefix) {
  if (string.size() >= prefix.size() && (string.compare(0, prefix.size(), prefix) == 0)) {
    return std::string_view(string.data() + prefix.size(), string.size() - prefix.size());
  }
  return std::nullopt;
}

}  // namespace

tftp_status Paver::OpenWrite(std::string_view filename, size_t size) {
  // Skip past the NB_IMAGE_PREFIX prefix.
  std::string_view host_filename;
  if (auto without_prefix = WithoutPrefix(filename, NB_IMAGE_PREFIX); without_prefix.has_value()) {
    host_filename = without_prefix.value();
  } else {
    fprintf(stderr, "netsvc: Missing '%s' prefix in '%.*s'\n", NB_IMAGE_PREFIX,
            static_cast<int>(filename.size()), filename.data());
    return TFTP_ERR_IO;
  }

  // Paving an image to disk.
  if (host_filename == NB_FVM_HOST_FILENAME) {
    printf("netsvc: Running FVM Paver\n");
    command_ = Command::kFvm;
  } else if (host_filename == NB_BOOTLOADER_HOST_FILENAME) {
    // WriteBootloader() has been replaced by WriteFirmware() with an empty
    // firmware type, but keep this function around for backwards-compatibility
    // until we don't use it anymore.
    printf("netsvc: Running BOOTLOADER Paver (firmware type '')\n");
    command_ = Command::kFirmware;
    firmware_type_[0] = '\0';
  } else if (auto type = WithoutPrefix(host_filename, NB_FIRMWARE_HOST_FILENAME_PREFIX);
             type.has_value()) {
    printf("netsvc: Running FIRMWARE Paver (firmware type '%.*s')\n",
           static_cast<int>(type->size()), type->data());
    if (type->length() >= sizeof(firmware_type_)) {
      fprintf(stderr, "netsvc: Firmware type '%.*s' is too long (max %zu)\n",
              static_cast<int>(type->size()), type->data(), sizeof(firmware_type_) - 1);
      return TFTP_ERR_INVALID_ARGS;
    }
    command_ = Command::kFirmware;
    memcpy(firmware_type_, type->data(), type->length());
    firmware_type_[type->length()] = '\0';
  } else if (host_filename == NB_ZIRCONA_HOST_FILENAME) {
    printf("netsvc: Running ZIRCON-A Paver\n");
    command_ = Command::kAsset;
    configuration_ = ::llcpp::fuchsia::paver::Configuration::A;
    asset_ = ::llcpp::fuchsia::paver::Asset::KERNEL;
  } else if (host_filename == NB_ZIRCONB_HOST_FILENAME) {
    printf("netsvc: Running ZIRCON-B Paver\n");
    command_ = Command::kAsset;
    configuration_ = ::llcpp::fuchsia::paver::Configuration::B;
    asset_ = ::llcpp::fuchsia::paver::Asset::KERNEL;
  } else if (host_filename == NB_ZIRCONR_HOST_FILENAME) {
    printf("netsvc: Running ZIRCON-R Paver\n");
    command_ = Command::kAsset;
    configuration_ = ::llcpp::fuchsia::paver::Configuration::RECOVERY;
    asset_ = ::llcpp::fuchsia::paver::Asset::KERNEL;
  } else if (host_filename == NB_VBMETAA_HOST_FILENAME) {
    printf("netsvc: Running VBMETA-A Paver\n");
    command_ = Command::kAsset;
    configuration_ = ::llcpp::fuchsia::paver::Configuration::A;
    asset_ = ::llcpp::fuchsia::paver::Asset::VERIFIED_BOOT_METADATA;
  } else if (host_filename == NB_VBMETAB_HOST_FILENAME) {
    printf("netsvc: Running VBMETA-B Paver\n");
    command_ = Command::kAsset;
    configuration_ = ::llcpp::fuchsia::paver::Configuration::B;
    asset_ = ::llcpp::fuchsia::paver::Asset::VERIFIED_BOOT_METADATA;
  } else if (host_filename == NB_VBMETAR_HOST_FILENAME) {
    printf("netsvc: Running VBMETA-R Paver\n");
    command_ = Command::kAsset;
    configuration_ = ::llcpp::fuchsia::paver::Configuration::RECOVERY;
    asset_ = ::llcpp::fuchsia::paver::Asset::VERIFIED_BOOT_METADATA;
  } else if (host_filename == NB_SSHAUTH_HOST_FILENAME) {
    printf("netsvc: Installing SSH authorized_keys\n");
    command_ = Command::kDataFile;
    strncpy(path_, "ssh/authorized_keys", PATH_MAX);
  } else if (host_filename == NB_INIT_PARTITION_TABLES_HOST_FILENAME) {
    if (size < sizeof(modify_partition_table_info_t)) {
      return ZX_ERR_BUFFER_TOO_SMALL;
    }
    printf("netsvc: Initializing partition tables\n");
    command_ = Command::kInitPartitionTables;
  } else if (host_filename == NB_WIPE_PARTITION_TABLES_HOST_FILENAME) {
    if (size < sizeof(modify_partition_table_info_t)) {
      return ZX_ERR_BUFFER_TOO_SMALL;
    }
    printf("netsvc: Wiping partition tables\n");
    command_ = Command::kWipePartitionTables;
  } else {
    fprintf(stderr, "netsvc: Unknown Paver\n");
    return TFTP_ERR_IO;
  }

  auto status = buffer_mapper_.CreateAndMap(size, "paver");
  if (status != ZX_OK) {
    printf("netsvc: unable to allocate and map buffer. Size - %lu, Error - %d\n", size, status);
    return status;
  }
  fbl::AutoCall buffer_cleanup([this]() { buffer_mapper_.Reset(); });

  zx::channel paver_local, paver_remote;
  status = zx::channel::create(0, &paver_local, &paver_remote);
  if (status != ZX_OK) {
    fprintf(stderr, "netsvc: Unable to create channel pair.\n");
    return TFTP_ERR_IO;
  }
  status = fdio_service_connect_at(svc_root_.get(), ::llcpp::fuchsia::paver::Paver::Name,
                                   paver_remote.release());
  if (status != ZX_OK) {
    fprintf(stderr, "netsvc: Unable to open /svc/%s.\n", ::llcpp::fuchsia::paver::Paver::Name);
    return TFTP_ERR_IO;
  }

  paver_svc_.emplace(std::move(paver_local));
  fbl::AutoCall svc_cleanup([&]() { paver_svc_.reset(); });

  size_ = size;

  buf_refcount_.store(2u);
  write_offset_.store(0ul);
  exit_code_.store(0);
  in_progress_.store(true);

  sync_completion_reset(&data_ready_);

  auto thread_fn = command_ == Command::kFvm
                       ? [](void* arg) { return static_cast<Paver*>(arg)->StreamBuffer(); }
                       : [](void* arg) { return static_cast<Paver*>(arg)->MonitorBuffer(); };
  if (thrd_create(&buf_thrd_, thread_fn, this) != thrd_success) {
    fprintf(stderr, "netsvc: unable to launch buffer stream/monitor thread\n");
    status = ZX_ERR_NO_RESOURCES;
    return status;
  }
  thrd_detach(buf_thrd_);
  svc_cleanup.cancel();
  buffer_cleanup.cancel();

  return TFTP_NO_ERROR;
}

tftp_status Paver::Write(const void* data, size_t* length, off_t offset) {
  if (!InProgress()) {
    printf("netsvc: paver exited prematurely with %s. Check the debuglog for more information.\n",
           zx_status_get_string(exit_code()));
    reset_exit_code();
    return TFTP_ERR_IO;
  }

  if ((static_cast<size_t>(offset) > size_) || (offset + *length) > size_) {
    return TFTP_ERR_INVALID_ARGS;
  }
  memcpy(&buffer()[offset], data, *length);
  size_t new_offset = offset + *length;
  write_offset_.store(new_offset);
  // Wake the paver thread, if it is waiting for data
  sync_completion_signal(&data_ready_);
  return TFTP_NO_ERROR;
}

void Paver::Close() {
  unsigned int refcount = std::atomic_fetch_sub(&buf_refcount_, 1u);
  if (refcount == 1) {
    buffer_mapper_.Reset();
  }
  // TODO: Signal thread to wake up rather than wait for it to timeout if
  // stream is closed before write is complete?
}

}  // namespace netsvc
