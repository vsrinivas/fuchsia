// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <fuchsia/hardware/block/partition/c/fidl.h>
#include <lib/fdio/unsafe.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/boot/sysconfig.h>
#include <zircon/device/block.h>
#include <zircon/hw/gpt.h>

#include <ddk/debug.h>
#include <kvstore/kvstore.h>

#define DEV_BLOCK "/dev/class/block"

typedef enum {
  OP_READ,
  OP_WRITE,
  OP_EDIT,
} sysconfig_op_t;

static const uint8_t sysconfig_guid[GPT_GUID_LEN] = GUID_SYS_CONFIG_VALUE;

static void usage(void) {
  fprintf(stderr,
          "Usage:\n"
          "    sysconfig read <section> [key]*\n"
          "    sysconfig write <section> [key=value]*\n"
          "    sysconfig edit <section> [key=value]*\n"
          "\n"
          "Where <section> is one of: {version-a, version-b, boot-default, boot-oneshot}\n"
          "\n"
          "read:    Print values for the specified keys. If no keys are provided after \"read\",\n"
          "         then all key/value pairs are printed.\n"
          "write:   Write the provided key/value pairs to the specified section.\n"
          "edit:    Write the provided key/value pairs to the specified section,\n"
          "         preserving any existing key/value pairs already in the partition\n");
}

// returns a file descriptor to the raw sysconfig partition
static int open_sysconfig(void) {
  struct dirent* de;
  DIR* dir = opendir(DEV_BLOCK);
  if (!dir) {
    printf("Error opening %s\n", DEV_BLOCK);
    return -1;
  }
  while ((de = readdir(dir)) != NULL) {
    if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
      continue;
    }
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", DEV_BLOCK, de->d_name);
    int fd = open(path, O_RDWR);
    if (fd < 0) {
      fprintf(stderr, "Error opening %s\n", path);
      continue;
    }
    fdio_t* io = fdio_unsafe_fd_to_io(fd);
    zx_handle_t device = fdio_unsafe_borrow_channel(io);
    fuchsia_hardware_block_partition_GUID guid;
    zx_status_t status;
    zx_status_t io_status =
        fuchsia_hardware_block_partition_PartitionGetTypeGuid(device, &status, &guid);
    fdio_unsafe_release(io);

    if (io_status != ZX_OK || status != ZX_OK) {
      continue;
    }
    if (memcmp(guid.value, sysconfig_guid, sizeof(sysconfig_guid))) {
      continue;
    }

    return fd;
  }
  closedir(dir);
  return -1;
}

static int print_func(void* cookie, const char* key, const char* value) {
  printf("%s=%s\n", key, value);
  return 0;
}

static int copy_func(void* cookie, const char* key, const char* value) {
  struct kvstore* kvs = cookie;

  // copy values to kvs if they aren't set already
  const char* new_value = kvs_get(kvs, key, NULL);
  if (new_value) {
    return 0;
  } else {
    return kvs_add(kvs, key, value);
  }
}

int main(int argc, char** argv) {
  int ret = 0;

  if (argc < 3) {
    usage();
    return -1;
  }

  // skip "sysconfig"
  argv++;
  argc--;

  const char* op_name = *argv++;
  argc--;
  sysconfig_op_t op;

  if (!strcmp(op_name, "read")) {
    op = OP_READ;
  } else if (!strcmp(op_name, "write")) {
    op = OP_WRITE;
  } else if (!strcmp(op_name, "edit")) {
    op = OP_EDIT;
  } else {
    usage();
    return -1;
  }

  off_t section_offset;
  const char* section = *argv++;
  argc--;
  if (!strcmp(section, "version-a")) {
    section_offset = ZX_SYSCONFIG_VERSION_A_OFFSET;
  } else if (!strcmp(section, "version-b")) {
    section_offset = ZX_SYSCONFIG_VERSION_B_OFFSET;
  } else if (!strcmp(section, "boot-default")) {
    section_offset = ZX_SYSCONFIG_BOOT_DEFAULT_OFFSET;
  } else if (!strcmp(section, "boot-oneshot")) {
    section_offset = ZX_SYSCONFIG_BOOT_ONESHOT_OFFSET;
  } else {
    usage();
    return -1;
  }

  int fd = open_sysconfig();
  if (fd < 0) {
    fprintf(stderr, "could not find sysconfig partition\n");
    return -1;
  }

  ret = lseek(fd, section_offset, SEEK_SET);
  if (ret < 0) {
    fprintf(stderr, "lseek failed\n");
    goto done;
  }

  uint8_t old_buffer[ZX_SYSCONFIG_KVSTORE_SIZE];
  uint8_t new_buffer[ZX_SYSCONFIG_KVSTORE_SIZE];

  if ((ret = read(fd, old_buffer, sizeof(old_buffer))) != sizeof(old_buffer)) {
    fprintf(stderr, "could not read sysconfig partition: %d\n", ret);
    goto done;
  }

  // we will read the current section into old_kvs and write new section from new_kvs
  struct kvstore old_kvs, new_kvs;

  ret = kvs_load(&old_kvs, old_buffer, sizeof(old_buffer));
  if (ret == KVS_ERR_PARSE_HDR) {
    if (op == OP_WRITE || op == OP_EDIT) {
      printf("initializing empty or corrupt sysconfig partition\n");
      kvs_init(&old_kvs, old_buffer, sizeof(old_buffer));
    } else {
      fprintf(stderr, "kvs_load failed: %d\n", ret);
      goto done;
    }
  } else if (ret < 0) {
    fprintf(stderr, "unexpected error %d from kvs_load\n", ret);
    goto done;
  }

  if (op == OP_WRITE || op == OP_EDIT) {
    kvs_init(&new_kvs, new_buffer, sizeof(new_buffer));
  }

  if (argc == 0 && op == OP_READ) {
    // print all key/value pairs
    kvs_foreach(&old_kvs, NULL, print_func);
    goto done;
  }

  while (argc > 0) {
    const char* arg = *argv++;
    argc--;
    char* equals = strchr(arg, '=');
    // we should only find an '=' if we are writing or editing
    if (!!equals == (op == OP_READ)) {
      usage();
      ret = -1;
      goto done;
    }

    if (op == OP_WRITE || op == OP_EDIT) {
      // separate arg into key and value strings
      *equals = 0;
      const char* key = arg;
      const char* value = equals + 1;
      ret = kvs_add(&new_kvs, key, value);
      if (ret < 0) {
        fprintf(stderr, "kvs_add failed: %d\n", ret);
        goto done;
      }
    } else {
      const char* key = arg;
      const char* value = kvs_get(&old_kvs, key, "");
      printf("%s=%s\n", key, value);
    }
  }

  if (op == OP_EDIT) {
    // copy the other key/value pairs from old_kvs to new_kvs
    ret = kvs_foreach(&old_kvs, &new_kvs, copy_func);
    if (ret < 0) {
      fprintf(stderr, "failed to copy existing values to new kvs: %d\n", ret);
      goto done;
    }
  }
  if (op == OP_WRITE || op == OP_EDIT) {
    kvs_save(&new_kvs);
    ret = lseek(fd, section_offset, SEEK_SET);
    if (ret < 0) {
      fprintf(stderr, "lseek failed\n");
      goto done;
    }
    if ((ret = write(fd, new_buffer, sizeof(new_buffer))) != sizeof(new_buffer)) {
      fprintf(stderr, "could not write sysconfig partition: %d\n", ret);
      goto done;
    }
  }

done:
  close(fd);
  return ret;
}
