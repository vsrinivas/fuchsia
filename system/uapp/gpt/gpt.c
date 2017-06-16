// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gpt/gpt.h>
#include <magenta/device/block.h>
#include <magenta/syscalls.h> // for mx_cprng_draw
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define FLAG_HIDDEN ((uint64_t) 0x2)

static int cgetc(void) {
    uint8_t ch;
    for (;;) {
        int r = read(0, &ch, 1);
        if (r < 0) return r;
        if (r == 1) return ch;
    }
}

static char* utf16_to_cstring(char* dst, const uint16_t* src, size_t len) {
    size_t i = 0;
    char* ptr = dst;
    while (i < len) {
        char c = src[i++] & 0x7f;
        if (!c) continue;
        *ptr++ = c;
    }
    return dst;
}

struct guid {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
};

static char* guid_to_cstring(char* dst, const uint8_t* src) {
    struct guid* guid = (struct guid*)src;
    sprintf(dst, "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X", guid->data1, guid->data2, guid->data3, guid->data4[0], guid->data4[1], guid->data4[2], guid->data4[3], guid->data4[4], guid->data4[5], guid->data4[6], guid->data4[7]);
    return dst;
}

static gpt_device_t* init(const char* dev, bool warn, int* out_fd) {
    int fd = open(dev, O_RDWR);
    if (fd < 0) {
        printf("error opening %s\n", dev);
        return NULL;
    }

    block_info_t info;
    ssize_t rc = ioctl_block_get_info(fd, &info);
    if (rc < 0) {
        printf("error getting block info\n");
        close(fd);
        return NULL;
    }

    printf("blocksize=0x%X blocks=%" PRIu64 "\n", info.block_size, info.block_count);

    if (warn) {
        printf("WARNING: You are about to permanently alter %s\n\n"
               "Type 'y' to continue, any other key to cancel\n", dev);

        int c = cgetc();
        if (c != 'y') {
            close(fd);
            return NULL;
        }
    }

    gpt_device_t* gpt;
    rc = gpt_device_init(fd, info.block_size, info.block_count, &gpt);
    if (rc < 0) {
        printf("error initializing GPT\n");
        close(fd);
        return NULL;
    }

    *out_fd = fd;
    return gpt;
}

static mx_status_t commit(gpt_device_t* gpt, int fd) {
    int rc = gpt_device_sync(gpt);
    if (rc) {
        printf("Error: GPT device sync failed.\n");
        return MX_ERR_INTERNAL;
    }
    rc = ioctl_block_rr_part(fd);
    if (rc) {
        printf("Error: GPT updated but device could not be rebound. Please reboot.\n");
        return MX_ERR_INTERNAL;
    }
    printf("GPT changes complete.\n");
    return 0;
}

static void dump_partitions(const char* dev) {
    int fd;
    gpt_device_t* gpt = init(dev, false, &fd);
    if (!gpt) return;

    if (!gpt->valid) {
        printf("No valid GPT found\n");
        goto done;
    }

    printf("Partition table is valid\n");

    uint64_t start, end;
    if (gpt_device_range(gpt, &start, &end)) {
        printf("Couldn't identify device range\n");
        goto done;
    }

    printf("GPT contains usable blocks from %" PRIu64 " to %" PRIu64" (inclusive)\n", start, end);

    gpt_partition_t* p;
    char name[GPT_GUID_STRLEN];
    char guid[GPT_GUID_STRLEN];
    char id[GPT_GUID_STRLEN];
    int i;
    for (i = 0; i < PARTITIONS_COUNT; i++) {
        p = gpt->partitions[i];
        if (!p) break;
        memset(name, 0, GPT_GUID_STRLEN);
        printf("Paritition %d: %s\n",
               i, utf16_to_cstring(name, (const uint16_t*)p->name, GPT_GUID_STRLEN - 1));
        printf("    Start: %" PRIu64 ", End: %" PRIu64 " (%" PRIu64 " blocks)\n",
               p->first, p->last, p->last - p->first + 1);
        printf("    id:   %s\n", guid_to_cstring(guid, (const uint8_t*)p->guid));
        printf("    type: %s\n", guid_to_cstring(id, (const uint8_t*)p->type));
    }
    printf("Total: %d partitions\n", i);

done:
    gpt_device_release(gpt);
    close(fd);
}

static void init_gpt(const char* dev) {
    int fd;
    gpt_device_t* gpt = init(dev, true, &fd);
    if (!gpt) return;

    // generate a default header
    gpt_partition_remove_all(gpt);
    commit(gpt, fd);
    gpt_device_release(gpt);
    close(fd);
}

static void add_partition(const char* dev, uint64_t start, uint64_t end, const char* name) {
    uint8_t guid[GPT_GUID_LEN];
    size_t sz;
    if (mx_cprng_draw(guid, GPT_GUID_LEN, &sz) != MX_OK)
        return;

    int fd;
    gpt_device_t* gpt = init(dev, true, &fd);
    if (!gpt) return;

    if (!gpt->valid) {
        // generate a default header
        if (commit(gpt, fd)) {
            return;
        }
    }

    uint8_t type[GPT_GUID_LEN];
    memset(type, 0xff, GPT_GUID_LEN);
    int rc = gpt_partition_add(gpt, name, type, guid, start, end - start + 1, 0);
    if (rc == 0) {
        printf("add partition: name=%s start=%" PRIu64 " end=%" PRIu64 "\n", name, start, end);
        commit(gpt, fd);
    }

    gpt_device_release(gpt);
    close(fd);
}

static void remove_partition(const char* dev, int n) {
    int fd;
    gpt_device_t* gpt = init(dev, true, &fd);
    if (!gpt) return;

    if (n >= PARTITIONS_COUNT) {
        return;
    }
    gpt_partition_t* p = gpt->partitions[n];
    if (!p) {
        return;
    }
    int rc = gpt_partition_remove(gpt, p->guid);
    if (rc == 0) {
        char name[GPT_GUID_STRLEN];
        printf("remove partition: n=%d name=%s\n", n,
               utf16_to_cstring(name, (const uint16_t*)p->name,
                                GPT_GUID_STRLEN - 1));
        commit(gpt, fd);
    }

    gpt_device_release(gpt);
    close(fd);
}

/*
 * Given a file descriptor used to read a gpt_device_t and the corresponding
 * gpt_device_t, first release the gpt_device_t and then close the FD.
 */
static void tear_down_gpt(int fd, gpt_device_t* gpt) {
    if (gpt != NULL) {
        gpt_device_release(gpt);
    }
    close(fd);
}

/*
 * Converts a GUID of the format xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx to
 * a properly arranged, 16 byte sequence. This takes care of flipping the byte
 * order section-wise for the first three sections (8 bytes total) of the GUID.
 * bytes_out should be a 16 byte array where the final output will be placed.
 * A bool is returned representing success of parsing the GUID. false will be
 * returned if the GUID string is the wrong length or contains invalid
 * characters.
 */
static bool parse_guid(char* guid, uint8_t* bytes_out) {
    if (strlen(guid) != GPT_GUID_STRLEN - 1) {
        printf("GUID length is wrong: %zd but expected %d\n", strlen(guid),
               (GPT_GUID_STRLEN - 1));
        return false;
    }

    // how many nibbles of the byte we've processed
    uint8_t nibbles = 0;
    // value to accumulate byte as we parse its two char nibbles
    uint8_t val = 0;
    // which byte we're parsing
    uint8_t out_idx = 0;
    uint8_t dashes = 0;

    for (int idx = 0; idx < GPT_GUID_STRLEN - 1; idx++) {
        char c = guid[idx];

        uint8_t char_val = 0;
        if (c == '-') {
            dashes++;
            continue;
        } else if (c >= '0' && c <= '9') {
            char_val = c - '0';
        } else if (c >= 'A' && c <= 'F') {
            char_val = c - 'A' + 10;
        } else if (c >= 'a' && c <= 'f') {
            char_val = c - 'a' + 10;
        } else {
            fprintf(stderr, "'%c' is not a valid GUID character\n", c);
            return false;
        }

        val += char_val << (4 * (1 - nibbles));

        if (++nibbles == 2) {
            bytes_out[out_idx++] = val;
            nibbles = 0;
            val = 0;
        }
    }

    if (dashes != 4) {
        printf("Error, incorrect number of hex characters.\n");
        return false;
    }

    // Shuffle bytes because endianness is swapped for certain sections
    uint8_t swap;
    swap = bytes_out[0];
    bytes_out[0] = bytes_out[3];
    bytes_out[3] = swap;
    swap = bytes_out[1];
    bytes_out[1] = bytes_out[2];
    bytes_out[2] = swap;

    swap = bytes_out[4];
    bytes_out[4] = bytes_out[5];
    bytes_out[5] = swap;

    swap = bytes_out[6];
    bytes_out[6] = bytes_out[7];
    bytes_out[7] = swap;

    return true;
}

/*
 * Give a path to a block device and a partition index into a GPT, load the GPT
 * information into memory and find the requested partition. This does all the
 * bounds and other error checking. If MX_OK is returned, the out parameters
 * will be set to valid values. If MX_OK is returned, the caller should close
 * fd_out after it is done using the GPT information.
 */
static mx_status_t get_gpt_and_part(char* path_device, long idx_part,
                                    bool warn, int* fd_out,
                                    gpt_device_t** gpt_out,
                                    gpt_partition_t** part_out) {
    if (idx_part < 0 || idx_part >= PARTITIONS_COUNT) {
        return MX_ERR_INVALID_ARGS;
    }

    int fd = -1;
    gpt_device_t* gpt = init(path_device, warn, &fd);
    if (gpt == NULL) {
        tear_down_gpt(fd, gpt);
        return MX_ERR_INTERNAL;
    }

    gpt_partition_t* part = gpt->partitions[idx_part];
    if (part == NULL) {
        tear_down_gpt(fd, gpt);
        return MX_ERR_INTERNAL;
    }

    *gpt_out = gpt;
    *part_out = part;
    *fd_out = fd;
    return MX_OK;
}

/*
 * Match keywords "BLOBFS", "DATA", "SYSTEM", or "EFI" and convert them to their
 * corresponding byte sequences. 'out' should point to a GPT_GUID_LEN array.
 */
static bool expand_special(char* in, uint8_t* out) {
    if (in == NULL) {
        return false;
    }

    static const uint8_t blobfs[GPT_GUID_LEN] = GUID_BLOBFS_VALUE;
    static const uint8_t data[GPT_GUID_LEN] = GUID_DATA_VALUE;
    static const uint8_t system[GPT_GUID_LEN] = GUID_SYSTEM_VALUE;
    static const uint8_t efi[GPT_GUID_LEN] = GUID_EFI_VALUE;

    int len = strlen(in);
    for (int i = 0; i < len; i++) in[i] = tolower(in[i]);

    if (len == 6 && !strncmp("blobfs", in, 6)) {
        memcpy(out, blobfs, GPT_GUID_LEN);
        return true;
    }

    if (len == 4 && !strncmp("data", in, 4)) {
        memcpy(out, data, GPT_GUID_LEN);
        return true;
    }

    if (len == 6 && !strncmp("system", in, 6)) {
        memcpy(out, system, GPT_GUID_LEN);
        return true;
    }

    if (len == 3 && !strncmp("efi", in, 3)) {
        memcpy(out, efi, GPT_GUID_LEN);
        return true;
    }

    return false;
}

/*
 * Edit a partition, changing either its type or ID GUID. path_device should be
 * the path to the device where the GPT can be read. idx_part should be the
 * index of the partition in the GPT that you want to change. guid should be the
 * string/human-readable form of the GUID and should be 36 characters plus a
 * null terminator.
 */
static mx_status_t edit_partition(char* path_device, long idx_part,
                                  char* type_or_id, char* guid) {
    gpt_device_t* gpt = NULL;
    gpt_partition_t* part = NULL;
    int fd = -1;

    mx_status_t rc = get_gpt_and_part(path_device, idx_part, true, &fd, &gpt,
                                      &part);
    if (rc != MX_OK) {
        return rc;
    }

    // whether we're setting the type or id GUID
    bool set_type;

    if (!strcmp(type_or_id, "type")) {
        set_type = true;
    } else if (!strcmp(type_or_id, "id")) {
        set_type = false;
    } else {
        tear_down_gpt(fd, gpt);
        return MX_ERR_INVALID_ARGS;
    }

    uint8_t guid_bytes[GPT_GUID_LEN];
    if (!expand_special(guid, guid_bytes) && !parse_guid(guid, guid_bytes)) {
        printf("GUID could not be parsed.\n");
        tear_down_gpt(fd, gpt);
        return MX_ERR_INVALID_ARGS;
    }

    if (set_type) {
        memcpy(part->type, guid_bytes, GPT_GUID_LEN);
    } else {
        memcpy(part->guid, guid_bytes, GPT_GUID_LEN);
    }

    rc = commit(gpt, fd);
    tear_down_gpt(fd, gpt);
    return rc;
}

/*
 * Set whether a partition is visible or not to the EFI firmware. If a
 * partition is set as hidden, the firmware will not attempt to boot from the
 * partition.
 */
static mx_status_t set_visibility(char* path_device, long idx_part,
                                  bool visible) {
    gpt_device_t* gpt = NULL;
    gpt_partition_t* part = NULL;
    int fd = -1;

    mx_status_t rc = get_gpt_and_part(path_device, idx_part, true, &fd, &gpt,
                                      &part);
    if (rc != MX_OK) {
        return rc;
    }

    if (visible) {
        part->flags &= ~FLAG_HIDDEN;
    } else {
        part->flags |= FLAG_HIDDEN;
    }

    rc = commit(gpt, fd);
    tear_down_gpt(fd, gpt);
    return rc;
}

// parse_size parses long integers in base 10, expanding p, t, g, m, and k
// suffices as binary byte scales. If the suffix is %, the value is returned as
// negative, in order to indicate a proportion.
static int64_t parse_size(char *s) {
  char *end = s;
  long long int v = strtoll(s, &end, 10);

  switch(*end) {
    case 0:
      break;
    case '%':
      v = -v;
      break;
    case 'p':
    case 'P':
      v *= 1024;
    case 't':
    case 'T':
      v *= 1024;
    case 'g':
    case 'G':
      v *= 1024;
    case 'm':
    case 'M':
      v *= 1024;
    case 'k':
    case 'K':
      v *= 1024;
  }
  return v;
}

// TODO(raggi): this should eventually get moved into ulib/gpt.
// align finds the next block at or after base that is aligned to a physical
// block boundary. The gpt specification requires that all partitions are
// aligned to physical block boundaries.
static uint64_t align(uint64_t base, uint64_t logical, uint64_t physical) {
  uint64_t a = logical;
  if (physical > a) a = physical;
  uint64_t base_bytes = base * logical;
  uint64_t d = base_bytes % a;
  return (base_bytes + a - d) / logical;
}

// repartition expects argv to start with the disk path and be followed by
// triples of name, type and size.
static int repartition(int argc, char** argv) {
  int fd = -1;
  ssize_t rc = 1;
  gpt_device_t* gpt = init(argv[0], false, &fd);
  if (!gpt) return 255;

  argc--;
  argv = &argv[1];
  int num_partitions = argc/3;

  gpt_partition_t *p = gpt->partitions[0];
  while (p) {
    gpt_partition_remove(gpt, p->guid);
    p = gpt->partitions[0];
  }


  block_info_t info;
  rc = ioctl_block_get_info(fd, &info);
  if (rc < 0) {
    printf("error getting block info\n");
    rc = 255;
    goto repartition_end;
  }
  uint64_t logical = info.block_size;
  uint64_t free_space = info.block_count * logical;

  {
    // expand out any proportional sizes into absolute sizes
    uint64_t sizes[num_partitions];
    memset(sizes, 0, sizeof(sizes));
    {
      uint64_t percent = 100;
      uint64_t portions[num_partitions];
      memset(portions, 0, sizeof(portions));
      for (int i = 0; i < num_partitions; i++) {
        int64_t sz = parse_size(argv[(i*3)+2]);
        if (sz > 0) {
          sizes[i] = sz;
          free_space -= sz;
        } else {
          if (percent == 0) {
            printf("more than 100%% of free space requested\n");
            rc = 1;
            goto repartition_end;
          }
          // portions from parse_size are negative
          portions[i] = -sz;
          percent -= -sz;
        }
      }
      for (int i = 0; i < num_partitions; i++) {
        if (portions[i] != 0)
          sizes[i] = (free_space * portions[i]) / 100;
      }
    }

    // TODO(raggi): get physical block size...
    uint64_t physical = 8192;

    uint64_t first_usable = 0;
    uint64_t last_usable = 0;
    gpt_device_range(gpt, &first_usable, &last_usable);

    uint64_t start = align(first_usable, logical, physical);

    for (int i = 0; i < num_partitions; i++) {
      char *name = argv[i*3];
      char *type_string = argv[i*3+1];

      uint64_t byte_size = sizes[i];

      uint8_t type[GPT_GUID_LEN];
      if (!expand_special(type_string, type) && !parse_guid(type_string, type)) {
          printf("GUID could not be parsed: %s\n", type_string);
          rc = 1;
          goto repartition_end;
      }

      uint8_t guid[GPT_GUID_LEN];
      size_t sz;
      if (mx_cprng_draw(guid, GPT_GUID_LEN, &sz) != MX_OK) {
        printf("rand read error\n");
        rc = 255;
        goto repartition_end;
      }

      // end is clamped to the sector before the next aligned partition, in order
      // to avoid wasting alignment space at the tail of partitions.
      uint64_t nblocks = (byte_size/logical) + (byte_size%logical == 0 ? 0 : 1);
      uint64_t end = align(start+nblocks+1, logical, physical) - 1;
      if (end > last_usable) end = last_usable;

      if (start > last_usable) {
        printf("partition %s does not fit\n", name);
        rc = 1;
        goto repartition_end;
      }

      printf("%s: %lu bytes, %lu blocks, %lu-%lu\n", name, byte_size, nblocks, start, end);
      gpt_partition_add(gpt, name, type, guid, start, end - start, 0);

      start = end + 1;
    }
  }

  rc = commit(gpt, fd);
repartition_end:
  gpt_device_release(gpt);
  close(fd);
  return rc;
}

int main(int argc, char** argv) {
    if (argc == 1) goto usage;

    const char* cmd = argv[1];
    if (!strcmp(cmd, "dump")) {
        if (argc <= 2) goto usage;
        dump_partitions(argv[2]);
    } else if (!strcmp(cmd, "init")) {
        if (argc <= 2) goto usage;
        init_gpt(argv[2]);
    } else if (!strcmp(cmd, "add")) {
        if (argc <= 5) goto usage;
        add_partition(argv[5], strtoull(argv[2], NULL, 0), strtoull(argv[3], NULL, 0), argv[4]);
    } else if (!strcmp(cmd, "remove")) {
        if (argc <= 3) goto usage;
        remove_partition(argv[3], strtol(argv[2], NULL, 0));
    } else if (!strcmp(cmd, "edit")) {
        if (argc <= 5) goto usage;
        if (edit_partition(argv[5], strtol(argv[2], NULL, 0), argv[3], argv[4])) {
            printf("Failed to edit partition.\n");
        }
    } else if (!strcmp(cmd, "visible")) {
        if (argc < 5) goto usage;
        bool visible;
        if (!strcmp(argv[3], "true")) {
            visible = true;
        } else if (!strcmp(argv[3], "false")) {
            visible = false;
        } else {
            goto usage;
        }

        if (set_visibility(argv[4], strtol(argv[2], NULL, 0), visible)) {
            printf("Error changing visibility.\n");
        }
    } else if (!strcmp(cmd, "repartition")) {
      if (argc < 6) goto usage;
      if (argc  % 3 != 0) goto usage;
      return repartition(argc-2, &argv[2]);
    } else {
        goto usage;
    }

    return 0;
usage:
    printf("usage:\n");
    printf("Note that for all these commands, [<dev>] is the device containing the GPT.\n");
    printf("Although using a GPT will split your device into small partitions, [<dev>] \n");
    printf("should always refer to the containing device, NOT block devices representing\n");
    printf("the partitions themselves.\n\n");
    printf("> %s dump [<dev>]\n", argv[0]);
    printf("  View the properties of the selected device\n");
    printf("> %s init [<dev>]\n", argv[0]);
    printf("  Initialize the block device with a GPT\n");
    printf("> %s repartition <dev> [[<label> <type> <size>], ...]\n", argv[0]);
    printf("  Destructively repartition the device with the given layout\n");
    printf("    e.g.\n");
    printf("    %s repartition /dev/class/block-core/000", argv[0]);
    printf(" esp efi 100m sys system 5g blob blobfs 50%% data data 50%%\n");
    printf("> %s add <start block> <end block> <name> [<dev>]\n", argv[0]);
    printf("  Add a partition to the device (and create a GPT if one does not exist)\n");
    printf("  Range of blocks is INCLUSIVE (both start and end). Full device range\n");
    printf("  may be queried using '%s dump'\n", argv[0]);
    printf("> %s edit <n> type|id BLOBFS|DATA|SYSTEM|EFI|<guid> [<dev>]\n", argv[0]);
    printf("  Edit the GUID of the nth partition on the device\n");
    printf("> %s remove <n> [<dev>]\n", argv[0]);
    printf("  Remove the nth partition from the device\n");
    printf("> %s visible <n> true|false [<dev>]\n", argv[0]);
    printf("  Set the visibility of the nth partition on the device\n");
    return 0;
}
