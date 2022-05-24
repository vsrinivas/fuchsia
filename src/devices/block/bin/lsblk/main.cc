// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.hardware.block.partition/cpp/wire.h>
#include <fidl/fuchsia.hardware.block/cpp/wire.h>
#include <fidl/fuchsia.hardware.skipblock/cpp/wire.h>
#include <inttypes.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/io.h>
#include <lib/fit/defer.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/zx/vmo.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/device/block.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <memory>
#include <string>

#include <fbl/unique_fd.h>
#include <gpt/c/gpt.h>
#include <gpt/guid.h>
#include <pretty/hexdump.h>
#include <storage-metrics/block-metrics.h>

#define DEV_BLOCK "/dev/class/block"
#define DEV_SKIP_BLOCK "/dev/class/skip-block"

namespace fuchsia_block = fuchsia_hardware_block;
namespace fuchsia_partition = fuchsia_hardware_block_partition;
namespace fuchsia_skipblock = fuchsia_hardware_skipblock;

static char* size_to_cstring(char* str, size_t maxlen, uint64_t size) {
  const char* unit;
  uint64_t div;
  if (size < 1024) {
    unit = "";
    div = 1;
  } else if (size >= 1024 && size < 1024 * 1024) {
    unit = "K";
    div = 1024;
  } else if (size >= 1024 * 1024 && size < 1024 * 1024 * 1024) {
    unit = "M";
    div = 1024 * 1024;
  } else if (size >= 1024 * 1024 * 1024 && size < 1024llu * 1024 * 1024 * 1024) {
    unit = "G";
    div = 1024 * 1024 * 1024;
  } else {
    unit = "T";
    div = 1024llu * 1024 * 1024 * 1024;
  }
  snprintf(str, maxlen, "%" PRIu64 "%s", size / div, unit);
  return str;
}

typedef struct blkinfo {
  char path[128];
  char topo[1024];
  char label[fuchsia_partition::wire::kNameLength + 1];
  char sizestr[6];
} blkinfo_t;

static void populate_topo_path(fidl::UnownedClientEnd<fuchsia_device::Controller> client,
                               blkinfo_t* info) {
  size_t path_len;

  auto resp = fidl::WireCall(client)->GetTopologicalPath();

  if (resp.status() != ZX_OK || resp.Unwrap_NEW()->is_error()) {
    strcpy(info->topo, "UNKNOWN");
    return;
  }

  path_len = resp.Unwrap_NEW()->value()->path.size();
  auto& r = *resp.Unwrap_NEW()->value();
  memcpy(info->topo, r.path.data(), r.path.size());

  info->topo[path_len] = '\0';
}

static int cmd_list_blk(void) {
  struct dirent* de;
  DIR* dir = opendir(DEV_BLOCK);
  if (!dir) {
    fprintf(stderr, "Error opening %s\n", DEV_BLOCK);
    return -1;
  }
  auto cleanup = fit::defer([&dir]() { closedir(dir); });

  blkinfo_t info;
  printf("%-3s %-4s %-16s %-20s %-6s %s\n", "ID", "SIZE", "TYPE", "LABEL", "FLAGS", "DEVICE");

  while ((de = readdir(dir)) != NULL) {
    if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
      continue;
    }
    memset(&info, 0, sizeof(blkinfo_t));
    snprintf(info.path, sizeof(info.path), "%s/%s", DEV_BLOCK, de->d_name);
    fbl::unique_fd fd(open(info.path, O_RDONLY));
    if (!fd) {
      fprintf(stderr, "Error opening %s\n", info.path);
      continue;
    }
    fdio_cpp::FdioCaller caller(std::move(fd));

    populate_topo_path(caller.borrow_as<fuchsia_device::Controller>(), &info);

    fuchsia_block::wire::BlockInfo block_info;

    auto info_resp = fidl::WireCall(caller.borrow_as<fuchsia_block::Block>())->GetInfo();

    if (info_resp.ok() && info_resp.value_NEW().status == ZX_OK && info_resp.value_NEW().info) {
      block_info = *info_resp.value_NEW().info;
      size_to_cstring(
          info.sizestr, sizeof(info.sizestr),
          info_resp.value_NEW().info->block_size * info_resp.value_NEW().info->block_count);
    }

    std::string type;
    auto guid_resp =
        fidl::WireCall(caller.borrow_as<fuchsia_partition::Partition>())->GetTypeGuid();
    if (guid_resp.ok() && guid_resp.value_NEW().status == ZX_OK && guid_resp.value_NEW().guid) {
      type = gpt::KnownGuid::TypeDescription(guid_resp.value_NEW().guid->value.data());
    }

    auto name_resp = fidl::WireCall(caller.borrow_as<fuchsia_partition::Partition>())->GetName();
    if (name_resp.ok() && name_resp.value_NEW().status == ZX_OK) {
      size_t truncated_name_len = name_resp.value_NEW().name.size() <= sizeof(info.label) - 1
                                      ? name_resp.value_NEW().name.size()
                                      : sizeof(info.label) - 1;
      strncpy(info.label, name_resp.value_NEW().name.begin(), truncated_name_len);
      info.label[truncated_name_len] = '\0';
    } else {
      info.label[0] = '\0';
    }
    char flags[20] = {0};
    if (block_info.flags & BLOCK_FLAG_READONLY) {
      strlcat(flags, "RO ", sizeof(flags));
    }
    if (block_info.flags & BLOCK_FLAG_REMOVABLE) {
      strlcat(flags, "RE ", sizeof(flags));
    }
    if (block_info.flags & BLOCK_FLAG_BOOTPART) {
      strlcat(flags, "BP ", sizeof(flags));
    }
    printf("%-3s %4s %-16s %-20s %-6s %s\n", de->d_name, info.sizestr, type.c_str(), info.label,
           flags, info.topo);
  }
  return 0;
}

static int cmd_list_skip_blk(void) {
  struct dirent* de;
  DIR* dir = opendir(DEV_SKIP_BLOCK);
  if (!dir) {
    fprintf(stderr, "Error opening %s\n", DEV_SKIP_BLOCK);
    return -1;
  }
  blkinfo_t info;
  while ((de = readdir(dir)) != NULL) {
    if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
      continue;
    }
    memset(&info, 0, sizeof(blkinfo_t));
    snprintf(info.path, sizeof(info.path), "%s/%s", DEV_SKIP_BLOCK, de->d_name);
    fbl::unique_fd fd(open(info.path, O_RDONLY));
    if (!fd) {
      fprintf(stderr, "Error opening %s\n", info.path);
      continue;
    }
    fdio_cpp::FdioCaller caller(std::move(fd));

    populate_topo_path(caller.borrow_as<fuchsia_device::Controller>(), &info);

    std::string type;
    auto result =
        fidl::WireCall(caller.borrow_as<fuchsia_skipblock::SkipBlock>())->GetPartitionInfo();
    if (result.ok() && result.value_NEW().status == ZX_OK) {
      size_to_cstring(info.sizestr, sizeof(info.sizestr),
                      result.value_NEW().partition_info.block_size_bytes *
                          result.value_NEW().partition_info.partition_block_count);
      type =
          gpt::KnownGuid::TypeDescription(result.value_NEW().partition_info.partition_guid.data());
    }

    printf("%-3s %4s %-16s %-20s %-6s %s\n", de->d_name, info.sizestr, type.c_str(), "", "",
           info.topo);
  }
  closedir(dir);
  return 0;
}

static int try_read_skip_blk(const fdio_cpp::UnownedFdioCaller& caller, off_t offset,
                             size_t count) {
  // check that count and offset are aligned to block size
  uint64_t blksize;
  zx_status_t status;
  auto result =
      fidl::WireCall(caller.borrow_as<fuchsia_skipblock::SkipBlock>())->GetPartitionInfo();
  if (result.status() != ZX_OK) {
    return result.status();
  }
  if (result.value_NEW().status != ZX_OK) {
    return result.value_NEW().status;
  }
  blksize = result.value_NEW().partition_info.block_size_bytes;
  if (count % blksize) {
    fprintf(stderr, "Bytes read must be a multiple of blksize=%" PRIu64 "\n", blksize);
    return -1;
  }
  if (offset % blksize) {
    fprintf(stderr, "Offset must be a multiple of blksize=%" PRIu64 "\n", blksize);
    return -1;
  }

  // allocate and map a buffer to read into
  zx::vmo vmo;
  if (zx::vmo::create(count, 0, &vmo) != ZX_OK) {
    fprintf(stderr, "No memory\n");
    return -1;
  }

  fzl::OwnedVmoMapper mapper;
  status = mapper.Map(std::move(vmo), count);
  if (status != ZX_OK) {
    fprintf(stderr, "Failed to map vmo\n");
    return -1;
  }
  zx::vmo dup;
  if (mapper.vmo().duplicate(ZX_RIGHT_SAME_RIGHTS, &dup) != ZX_OK) {
    fprintf(stderr, "Cannot duplicate handle\n");
    return -1;
  }

  // read the data
  auto read_result = fidl::WireCall(caller.borrow_as<fuchsia_skipblock::SkipBlock>())
                         ->Read(fuchsia_skipblock::wire::ReadWriteOperation{
                             .vmo = std::move(dup),
                             .vmo_offset = 0,
                             .block = static_cast<uint32_t>(offset / blksize),
                             .block_count = static_cast<uint32_t>(count / blksize),
                         });
  if (read_result.status() != ZX_OK) {
    status = read_result.status();
  } else {
    status = read_result.value_NEW().status;
  }
  if (status != ZX_OK) {
    fprintf(stderr, "Error %d in SkipBlockRead()\n", status);
    return status;
  }

  hexdump8_ex(mapper.start(), count, offset);

  return ZX_OK;
}

static int cmd_read_blk(const char* dev, off_t offset, size_t count) {
  fbl::unique_fd fd(open(dev, O_RDONLY));
  if (!fd) {
    fprintf(stderr, "Error opening %s\n", dev);
    return -1;
  }
  fdio_cpp::UnownedFdioCaller caller(fd.get());

  // Try querying for block info on a new channel.
  // lsblk also supports reading from skip block devices, but guessing the "wrong" type
  // of FIDL protocol will close the communication channel.
  zx::channel maybe_block(fdio_service_clone(caller.borrow_channel()));
  fuchsia_hardware_block_BlockInfo info;
  zx_status_t status;
  zx_status_t io_status = fuchsia_hardware_block_BlockGetInfo(maybe_block.get(), &status, &info);
  if (io_status != ZX_OK) {
    status = io_status;
  }
  if (status != ZX_OK) {
    if (try_read_skip_blk(caller, offset, count) < 0) {
      fprintf(stderr, "Error getting block size for %s\n", dev);
      return -1;
    }
    return 0;
  }
  // Check that count and offset are aligned to block size.
  uint64_t blksize = info.block_size;
  if (count % blksize) {
    fprintf(stderr, "Bytes read must be a multiple of blksize=%" PRIu64 "\n", blksize);
    return -1;
  }
  if (offset % blksize) {
    fprintf(stderr, "Offset must be a multiple of blksize=%" PRIu64 "\n", blksize);
    return -1;
  }

  // read the data
  std::unique_ptr<uint8_t[]> buf(new uint8_t[count]);
  if (offset) {
    off_t rc = lseek(fd.get(), offset, SEEK_SET);
    if (rc < 0) {
      fprintf(stderr, "Error %lld seeking to offset %jd\n", rc, (intmax_t)offset);
      return -1;
    }
  }
  ssize_t c = read(fd.get(), buf.get(), count);
  if (c < 0) {
    fprintf(stderr, "Error %zd in read()\n", c);
    return -1;
  }

  hexdump8_ex(buf.get(), c, offset);
  return 0;
}

static int cmd_stats(const char* dev, bool clear) {
  fbl::unique_fd fd(open(dev, O_RDONLY));
  if (!fd) {
    fprintf(stderr, "Error opening %s\n", dev);
    return -1;
  }
  fdio_cpp::FdioCaller caller(std::move(fd));
  auto result = fidl::WireCall(caller.borrow_as<fuchsia_block::Block>())->GetStats(clear);
  if (!result.ok() || result.value_NEW().status != ZX_OK) {
    fprintf(stderr, "Error getting stats for %s\n", dev);
    return -1;
  }
  storage_metrics::BlockDeviceMetrics metrics(result.value_NEW().stats.get());
  metrics.Dump(stdout);
  return 0;
}

int main(int argc, const char** argv) {
  int rc = 0;
  const char* cmd = argc > 1 ? argv[1] : NULL;
  if (cmd) {
    if (!strcmp(cmd, "help")) {
      goto usage;
    } else if (!strcmp(cmd, "read")) {
      if (argc < 5)
        goto usage;
      rc = cmd_read_blk(argv[2], strtoul(argv[3], NULL, 10), strtoull(argv[4], NULL, 10));
    } else if (!strcmp(cmd, "stats")) {
      if (argc < 4)
        goto usage;
      if (strcmp("true", argv[3]) && strcmp("false", argv[3]))
        goto usage;
      rc = cmd_stats(argv[2], !strcmp("true", argv[3]) ? true : false);
    } else {
      fprintf(stderr, "Unrecognized command %s!\n", cmd);
      goto usage;
    }
  } else {
    rc = cmd_list_blk() || cmd_list_skip_blk();
  }
  return rc;
usage:
  fprintf(stderr, "Usage:\n");
  fprintf(stderr, "%s\n", argv[0]);
  fprintf(stderr, "%s read <blkdev> <offset> <count>\n", argv[0]);
  fprintf(stderr, "%s stats <blkdev> <clear=true|false>\n", argv[0]);
  return 0;
}
