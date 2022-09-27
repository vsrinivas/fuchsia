// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bringup/bin/netsvc/paver.h"

#include <fcntl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async-loop/loop.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fit/defer.h>
#include <lib/netboot/netboot.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/zx/clock.h>
#include <stdio.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <algorithm>
#include <string_view>

#include <fbl/algorithm.h>

#include "src/bringup/bin/netsvc/payload-streamer.h"

namespace netsvc {

Paver* Paver::Get() {
  static Paver* instance_ = nullptr;
  if (instance_ == nullptr) {
    zx::status client_end = component::Connect<fuchsia_io::Directory>("/svc");
    if (client_end.is_error()) {
      return nullptr;
    }
    fbl::unique_fd devfs_root(open("/dev", O_RDONLY));
    if (!devfs_root) {
      return nullptr;
    }

    instance_ = new Paver(std::move(client_end.value()), std::move(devfs_root));
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
      if (aborted_) {
        printf("netsvc: 1 paver aborted, exiting copy thread\n");
        exit_code_.store(ZX_ERR_CANCELED);
        result = TFTP_ERR_BAD_STATE;
        return ZX_ERR_CANCELED;
      }
      write_offset = write_offset_.load();
    };
    size = std::min(size, write_offset - read_offset);
    memcpy(buf, buffer() + read_offset, size);
    *actual = size;

    // Best effort try to decommit pages we have already copied. This will prevent us from
    // running out of memory.
    ZX_ASSERT(read_offset + size > decommitted_offset);
    const size_t decommit_size =
        fbl::round_down(read_offset + size - decommitted_offset, zx_system_get_page_size());
    // TODO(surajmalhotra): Tune this in case we decommit too aggressively.
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

  auto cleanup = fit::defer([this, &result]() {
    ClearBufferRef(kBufferRefWorker);

    paver_svc_ = {};
    fshost_admin_svc_ = {};

    if (result != 0) {
      printf("netsvc: copy exited prematurely (%d): expect paver errors\n", result);
    }

    in_progress_.store(false);
  });

  zx::status data_sink = fidl::CreateEndpoints<fuchsia_paver::DataSink>();
  if (data_sink.is_error()) {
    fprintf(stderr, "netsvc: unable to create channel\n");
    exit_code_.store(data_sink.status_value());
    return 0;
  }

  fidl::WireResult res = paver_svc_->FindDataSink(std::move(data_sink->server));
  if (!res.ok()) {
    fprintf(stderr, "netsvc: unable to find data sink\n");
    exit_code_.store(res.status());
    return 0;
  }

  zx::status payload_stream = fidl::CreateEndpoints<fuchsia_paver::PayloadStream>();
  if (payload_stream.is_error()) {
    fprintf(stderr, "netsvc: unable to create channel\n");
    exit_code_.store(payload_stream.status_value());
    return 0;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  PayloadStreamer streamer(std::move(payload_stream->server), std::move(callback));
  loop.StartThread("payload-streamer");

  // Blocks until paving is complete.
  fidl::WireResult res2 =
      fidl::WireCall(data_sink->client)->WriteVolumes(std::move(payload_stream->client));
  zx_status_t status = res2.ok() ? res2.value().status : res2.status();

  exit_code_.store(status);

  // Shutdown the loop. Destructor ordering will destroy the streamer first, so
  // we must force the loop to stop before then.
  loop.Shutdown();
  loop.JoinThreads();
  return 0;
}

namespace {

using WriteFirmwareResult = fidl::WireResult<fuchsia_paver::DataSink::WriteFirmware>;

zx_status_t ProcessWriteFirmwareResult(const WriteFirmwareResult& res, const char* firmware_type) {
  if (!res.ok()) {
    return res.status();
  } else if (res.value().result.is_status()) {
    return res.value().result.status();
  } else if (res.value().result.is_unsupported()) {
    // Log a message but just skip this, we want to keep going so that we
    // can add new firmware types in the future without breaking older
    // paver versions.
    printf("netsvc: skipping unsupported firmware type '%s'\n", firmware_type);
    return ZX_OK;
  } else {
    // We must have added another union field but forgot to update this code.
    fprintf(stderr, "netsvc: unknown WriteFirmware result\n");
    return ZX_ERR_INTERNAL;
  }
}

}  // namespace

zx_status_t Paver::WriteABImage(fidl::WireSyncClient<fuchsia_paver::DataSink> data_sink,
                                fuchsia_mem::wire::Buffer buffer) {
  zx::status endpoints = fidl::CreateEndpoints<fuchsia_paver::BootManager>();
  if (endpoints.is_error()) {
    fprintf(stderr, "netsvc: unable to create channel\n");
    exit_code_.store(endpoints.status_value());
    return 0;
  }

  fidl::WireResult res = paver_svc_->FindBootManager(std::move(endpoints->server));
  if (!res.ok()) {
    fprintf(stderr, "netsvc: unable to find boot manager\n");
    exit_code_.store(res.status());
    return 0;
  }
  fidl::WireSyncClient<fuchsia_paver::BootManager> boot_manager =
      fidl::WireSyncClient(std::move(endpoints->client));

  // First find out whether or not ABR is supported.
  {
    auto result = boot_manager->QueryActiveConfiguration();
    if (result.status() != ZX_OK) {
      boot_manager = {};
    }
  }

  // Make sure to mark the configuration we are about to pave as no longer bootable.
  if (boot_manager && configuration_ != fuchsia_paver::wire::Configuration::kRecovery) {
    auto result = boot_manager->SetConfigurationUnbootable(configuration_);
    auto status = result.ok() ? result.value().status : result.status();
    if (status != ZX_OK) {
      fprintf(stderr, "netsvc: Unable to set configuration as unbootable.\n");
      return status;
    }
  }
  if (command_ == Command::kAsset) {
    auto result = data_sink->WriteAsset(configuration_, asset_, std::move(buffer));
    auto status = result.ok() ? result.value().status : result.status();
    if (status != ZX_OK) {
      fprintf(stderr, "netsvc: Unable to write asset. %s\n", zx_status_get_string(status));
      return status;
    }
  } else if (command_ == Command::kFirmware) {
    auto result = data_sink->WriteFirmware(configuration_,
                                           fidl::StringView(firmware_type_, strlen(firmware_type_)),
                                           std::move(buffer));
    if (auto status = ProcessWriteFirmwareResult(result, firmware_type_); status != ZX_OK) {
      return status;
    }
  }
  // Set configuration A/B as default.
  // We assume that verified boot metadata asset will only be written after the kernel asset.
  if (!boot_manager || configuration_ == fuchsia_paver::wire::Configuration::kRecovery ||
      command_ == Command::kFirmware ||
      asset_ != fuchsia_paver::wire::Asset::kVerifiedBootMetadata) {
    if (boot_manager) {
      auto res = boot_manager->Flush();
      auto status_sync = res.ok() ? res.value().status : res.status();
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
    auto status = result.ok() ? result.value().status : result.status();
    if (status != ZX_OK) {
      fprintf(stderr, "netsvc: Unable to set configuration as active.\n");
      return status;
    }
  }
  {
    auto opposite = configuration_ == fuchsia_paver::wire::Configuration::kA
                        ? fuchsia_paver::wire::Configuration::kB
                        : fuchsia_paver::wire::Configuration::kA;

    auto result = boot_manager->SetConfigurationUnbootable(opposite);
    auto status = result.ok() ? result.value().status : result.status();
    if (status != ZX_OK) {
      fprintf(stderr, "netsvc: Unable to set opposite configuration as unbootable.\n");
      return status;
    }
  }

  // TODO(fxbug.dev/47505): The following two syncs are called everytime WriteAsset is called, which
  // is not optimal for reducing NAND PE cycles. Ideally, we want to sync when all assets, A/B
  // configuration have been written to buffer. Find a safe time and place for sync.
  {
    auto res = data_sink->Flush();
    auto status = res.ok() ? res.value().status : res.status();
    if (status != ZX_OK) {
      fprintf(stderr, "netsvc: failed to flush data_sink. %s\n", zx_status_get_string(status));
      return status;
    }
  }

  if (boot_manager) {
    auto res = boot_manager->Flush();
    auto status = res.ok() ? res.value().status : res.status();
    if (status != ZX_OK) {
      fprintf(stderr, "netsvc: failed to flush A/B/R configuration. %s\n",
              zx_status_get_string(status));
      return status;
    }
  }

  return ClearSysconfig();
}

zx_status_t Paver::ClearSysconfig() {
  zx::status endpoints = fidl::CreateEndpoints<fuchsia_paver::Sysconfig>();
  if (endpoints.is_error()) {
    fprintf(stderr, "netsvc: unable to create channel\n");
    return endpoints.status_value();
  }

  fidl::WireResult status_find_sysconfig = paver_svc_->FindSysconfig(std::move(endpoints->server));
  if (!status_find_sysconfig.ok()) {
    fprintf(stderr, "netsvc: unable to find sysconfig\n");
    return status_find_sysconfig.status();
  }

  fidl::WireSyncClient client{std::move(endpoints->client)};

  auto wipe_result = client->Wipe();
  auto wipe_status =
      wipe_result.status() == ZX_OK ? wipe_result.value().status : wipe_result.status();
  if (wipe_status == ZX_ERR_PEER_CLOSED) {
    // If the first fidl call after connection returns ZX_ERR_PEER_CLOSED,
    // consider it as not supported.
    fprintf(stderr, "netsvc: sysconfig is not supported\n");
    return ZX_OK;
  } else if (wipe_status != ZX_OK) {
    fprintf(stderr, "netsvc: Failed to wipe sysconfig partition.\n");
    return wipe_status;
  }

  auto flush_result = client->Flush();
  auto flush_status =
      flush_result.status() == ZX_OK ? flush_result.value().status : flush_result.status();
  if (flush_status != ZX_OK) {
    fprintf(stderr, "netsvc: Failed to flush sysconfig partition.\n");
    return flush_status;
  }

  return ZX_OK;
}

zx_status_t Paver::OpenDataSink(fuchsia_mem::wire::Buffer buffer,
                                fidl::WireSyncClient<fuchsia_paver::DynamicDataSink>* data_sink) {
  modify_partition_table_info_t partition_info = {};
  auto status = buffer.vmo.read(&partition_info, 0, sizeof(partition_info));
  if (status != ZX_OK) {
    fprintf(stderr, "netsvc: Unable to read from vmo\n");
    return status;
  }
  if (partition_info.block_device_path[NB_PATH_MAX] != '\0') {
    fprintf(stderr, "netsvc: Invalid block device path specified\n");
    return ZX_ERR_INVALID_ARGS;
  }

  constexpr char kDevfsPrefix[] = "/dev/";
  if (strncmp(kDevfsPrefix, partition_info.block_device_path, strlen(kDevfsPrefix)) != 0) {
    fprintf(stderr, "netsvc: Invalid block device path specified %s\n",
            partition_info.block_device_path);
    return ZX_ERR_INVALID_ARGS;
  }

  fdio_cpp::UnownedFdioCaller caller(devfs_root_.get());

  zx::status client_end = component::ConnectAt<fuchsia_hardware_block::Block>(
      caller.directory(), &partition_info.block_device_path[5]);
  if (client_end.is_error()) {
    fprintf(stderr, "netsvc: Unable to open %s.\n", partition_info.block_device_path);
    return client_end.status_value();
  }

  zx::status endpoints = fidl::CreateEndpoints<fuchsia_paver::DynamicDataSink>();
  if (endpoints.is_error()) {
    fprintf(stderr, "netsvc: unable to create channel.\n");
    return endpoints.status_value();
  }

  fidl::WireResult res =
      paver_svc_->UseBlockDevice(std::move(client_end.value()), std::move(endpoints->server));
  if (!res.ok()) {
    fprintf(stderr, "netsvc: unable to use block device.\n");
    return res.status();
  }

  data_sink->Bind(std::move(endpoints->client));
  return ZX_OK;
}

zx_status_t Paver::InitPartitionTables(fuchsia_mem::wire::Buffer buffer) {
  fidl::WireSyncClient<fuchsia_paver::DynamicDataSink> data_sink;
  auto status = OpenDataSink(std::move(buffer), &data_sink);
  if (status != ZX_OK) {
    fprintf(stderr, "netsvc: Unable to open data sink.\n");
    return status;
  }

  auto result = data_sink->InitializePartitionTables();
  status = result.ok() ? result.value().status : result.status();
  if (status != ZX_OK) {
    fprintf(stderr, "netsvc: Unable to initialize partition tables.\n");
    return status;
  }
  return ZX_OK;
}

zx_status_t Paver::WipePartitionTables(fuchsia_mem::wire::Buffer buffer) {
  fidl::WireSyncClient<fuchsia_paver::DynamicDataSink> data_sink;
  auto status = OpenDataSink(std::move(buffer), &data_sink);
  if (status != ZX_OK) {
    fprintf(stderr, "netsvc: Unable to open data sink.\n");
    return status;
  }

  auto result = data_sink->WipePartitionTables();
  status = result.ok() ? result.value().status : result.status();
  if (status != ZX_OK) {
    fprintf(stderr, "netsvc: Unable to wipe partition tables.\n");
    return status;
  }
  return ZX_OK;
}

int Paver::MonitorBuffer() {
  int result = TFTP_NO_ERROR;

  auto cleanup = fit::defer([this, &result]() {
    ClearBufferRef(kBufferRefWorker);
    paver_svc_ = {};
    fshost_admin_svc_ = {};

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
    if (aborted_) {
      printf("netsvc: 2 paver aborted, exiting copy thread\n");
      exit_code_.store(ZX_ERR_CANCELED);
      result = TFTP_ERR_BAD_STATE;
      return result;
    }
    write_ndx = write_offset_.load();
  } while (write_ndx < size_);

  zx::vmo dup;
  auto status = buffer_mapper_.vmo().duplicate(ZX_RIGHT_SAME_RIGHTS, &dup);
  if (status != ZX_OK) {
    exit_code_.store(status);
    return 0;
  }
  status = dup.set_prop_content_size(buffer_mapper_.size());
  if (status != ZX_OK) {
    exit_code_.store(status);
    return 0;
  }

  fuchsia_mem::wire::Buffer buffer = {
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

  zx::status endpoints = fidl::CreateEndpoints<fuchsia_paver::DataSink>();
  if (endpoints.is_error()) {
    fprintf(stderr, "netsvc: unable to create channel\n");
    exit_code_.store(endpoints.status_value());
    return 0;
  }

  fidl::WireResult res = paver_svc_->FindDataSink(std::move(endpoints->server));
  if (!res.ok()) {
    fprintf(stderr, "netsvc: unable to find data sink\n");
    exit_code_.store(res.status());
    return 0;
  }
  fidl::WireSyncClient data_sink{std::move(endpoints->client)};

  // Blocks until paving is complete.
  switch (command_) {
    case Command::kDataFile: {
      auto res = fshost_admin_svc_->WriteDataFile(fidl::StringView(path_, strlen(path_)),
                                                  std::move(buffer.vmo));
      status = res.status() == ZX_OK ? (res.ok() ? ZX_OK : res->error_value()) : res.status();
      break;
    }
    case Command::kFirmware:
      [[fallthrough]];
    case Command::kAsset:
      status = WriteABImage(std::move(data_sink), std::move(buffer));
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

tftp_status Paver::ProcessAsFirmwareImage(std::string_view host_filename) {
  struct {
    const char* prefix;
    const char* config_suffix;
    fuchsia_paver::wire::Configuration config;
  } matches[] = {
      {NB_FIRMWARE_HOST_FILENAME_PREFIX, "", fuchsia_paver::wire::Configuration::kA},
      {NB_FIRMWAREA_HOST_FILENAME_PREFIX, "-A", fuchsia_paver::wire::Configuration::kA},
      {NB_FIRMWAREB_HOST_FILENAME_PREFIX, "-B", fuchsia_paver::wire::Configuration::kB},
      {NB_FIRMWARER_HOST_FILENAME_PREFIX, "-R", fuchsia_paver::wire::Configuration::kRecovery},
  };

  for (auto& match : matches) {
    auto type = WithoutPrefix(host_filename, match.prefix);
    if (!type.has_value()) {
      continue;
    }

    printf("netsvc: Running FIRMWARE%s Paver (firmware type '%.*s')\n", match.config_suffix,
           static_cast<int>(type->size()), type->data());

    if (type->length() >= sizeof(firmware_type_)) {
      fprintf(stderr, "netsvc: Firmware type '%.*s' is too long (max %zu)\n",
              static_cast<int>(type->size()), type->data(), sizeof(firmware_type_) - 1);
      return TFTP_ERR_INVALID_ARGS;
    }

    memcpy(firmware_type_, type->data(), type->length());
    firmware_type_[type->length()] = '\0';
    configuration_ = match.config;
    command_ = Command::kFirmware;
    return TFTP_NO_ERROR;
  }

  return TFTP_ERR_NOT_FOUND;
}

tftp_status Paver::OpenWrite(std::string_view filename, size_t size, zx::duration timeout) {
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
    configuration_ = fuchsia_paver::wire::Configuration::kA;
    firmware_type_[0] = '\0';
  } else if (auto status = ProcessAsFirmwareImage(host_filename); status != TFTP_ERR_NOT_FOUND) {
    if (status != TFTP_NO_ERROR) {
      return status;
    }
  } else if (host_filename == NB_ZIRCONA_HOST_FILENAME) {
    printf("netsvc: Running ZIRCON-A Paver\n");
    command_ = Command::kAsset;
    configuration_ = fuchsia_paver::wire::Configuration::kA;
    asset_ = fuchsia_paver::wire::Asset::kKernel;
  } else if (host_filename == NB_ZIRCONB_HOST_FILENAME) {
    printf("netsvc: Running ZIRCON-B Paver\n");
    command_ = Command::kAsset;
    configuration_ = fuchsia_paver::wire::Configuration::kB;
    asset_ = fuchsia_paver::wire::Asset::kKernel;
  } else if (host_filename == NB_ZIRCONR_HOST_FILENAME) {
    printf("netsvc: Running ZIRCON-R Paver\n");
    command_ = Command::kAsset;
    configuration_ = fuchsia_paver::wire::Configuration::kRecovery;
    asset_ = fuchsia_paver::wire::Asset::kKernel;
  } else if (host_filename == NB_VBMETAA_HOST_FILENAME) {
    printf("netsvc: Running VBMETA-A Paver\n");
    command_ = Command::kAsset;
    configuration_ = fuchsia_paver::wire::Configuration::kA;
    asset_ = fuchsia_paver::wire::Asset::kVerifiedBootMetadata;
  } else if (host_filename == NB_VBMETAB_HOST_FILENAME) {
    printf("netsvc: Running VBMETA-B Paver\n");
    command_ = Command::kAsset;
    configuration_ = fuchsia_paver::wire::Configuration::kB;
    asset_ = fuchsia_paver::wire::Asset::kVerifiedBootMetadata;
  } else if (host_filename == NB_VBMETAR_HOST_FILENAME) {
    printf("netsvc: Running VBMETA-R Paver\n");
    command_ = Command::kAsset;
    configuration_ = fuchsia_paver::wire::Configuration::kRecovery;
    asset_ = fuchsia_paver::wire::Asset::kVerifiedBootMetadata;
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
  auto buffer_cleanup = fit::defer([this]() { buffer_mapper_.Reset(); });

  auto paver = component::ConnectAt<fuchsia_paver::Paver>(svc_root_);
  if (paver.is_error()) {
    fprintf(stderr, "netsvc: Unable to open /svc/%s.\n",
            fidl::DiscoverableProtocolName<fuchsia_paver::Paver>);
    return TFTP_ERR_IO;
  }
  auto fshost = component::ConnectAt<fuchsia_fshost::Admin>(svc_root_);
  if (fshost.is_error()) {
    fprintf(stderr, "netsvc: Unable to open /svc/%s.\n",
            fidl::DiscoverableProtocolName<fuchsia_fshost::Admin>);
    return TFTP_ERR_IO;
  }

  paver_svc_ = fidl::WireSyncClient(std::move(*paver));
  fshost_admin_svc_ = fidl::WireSyncClient(std::move(*fshost));
  auto svc_cleanup = fit::defer([&]() {
    fshost_admin_svc_ = {};
    paver_svc_ = {};
  });

  size_ = size;

  buffer_refs_.store(kBufferRefWorker | kBufferRefApi);
  write_offset_.store(0ul);
  exit_code_.store(0);
  in_progress_.store(true);
  // Use a fixed multiplier on requested timeout based on empirical tests for
  // paving stability.
  timeout_ = timeout * 5;

  aborted_ = false;
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

void Paver::Close() { ClearBufferRef(kBufferRefApi); }

void Paver::Abort() {
  aborted_ = true;
  sync_completion_signal(&data_ready_);
}

void Paver::ClearBufferRef(uint32_t ref) {
  if (buffer_refs_.fetch_and(~ref) == ref) {
    buffer_mapper_.Reset();
  }
}

}  // namespace netsvc
