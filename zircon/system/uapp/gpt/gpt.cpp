// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <getopt.h>
#include <gpt/cros.h>
#include <gpt/gpt.h>
#include <zircon/device/block.h>
#include <zircon/syscalls.h> // for zx_cprng_draw
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

using gpt::GptDevice;

namespace {

constexpr uint64_t kFlagHidden = 0x2;

const char* bin_name;
bool confirm_writes = true;

zx_status_t ReadPartitionIndex(const char* arg, uint32_t* idx) {
    char* end;
    unsigned long lidx = strtoul(arg, &end, 10);
    if (*end != 0 || lidx > UINT32_MAX || lidx >= gpt::kPartitionCount) {
        return ZX_ERR_INVALID_ARGS;
    }

    *idx = static_cast<uint32_t>(lidx);
    return ZX_OK;
}

int status_to_retcode(zx_status_t ret) {
    return ret == ZX_OK ? 0 : 1;
}

int usage(zx_status_t ret) {
    printf("usage:\n");
    printf("Note that for all these commands, [<dev>] is the device containing the GPT.\n");
    printf("Although using a GPT will split your device into small partitions, [<dev>] \n");
    printf("should always refer to the containing device, NOT block devices representing\n");
    printf("the partitions themselves.\n\n");
    printf("> %s dump [<dev>]\n", bin_name);
    printf("  View the properties of the selected device\n");
    printf("> %s init [<dev>]\n", bin_name);
    printf("  Initialize the block device with a GPT\n");
    printf("> %s repartition <dev> [[<label> <type> <size>], ...]\n", bin_name);
    printf("  Destructively repartition the device with the given layout\n");
    printf("    e.g.\n");
    printf("    %s repartition /dev/class/block-core/000", bin_name);
    printf(" esp efi 100m sys system 5g blob blobfs 50%% data data 50%%\n");
    printf("> %s add <start block> <end block> <name> [<dev>]\n", bin_name);
    printf("  Add a partition to the device (and create a GPT if one does not exist)\n");
    printf("  Range of blocks is INCLUSIVE (both start and end). Full device range\n");
    printf("  may be queried using '%s dump'\n", bin_name);
    printf("> %s edit <n> type|id BLOBFS|DATA|SYSTEM|EFI|<guid> [<dev>]\n", bin_name);
    printf("  Edit the GUID of the nth partition on the device\n");
    printf("> %s edit_cros <n> [-T <tries>] [-S <successful>] [-P <priority] <dev>\n", bin_name);
    printf("  Edit the GUID of the nth partition on the device\n");
    printf("> %s adjust <n> <start block> <end block> [<dev>]\n", bin_name);
    printf("  Move or resize the nth partition on the device\n");
    printf("> %s remove <n> [<dev>]\n", bin_name);
    printf("  Remove the nth partition from the device\n");
    printf("> %s visible <n> true|false [<dev>]\n", bin_name);
    printf("  Set the visibility of the nth partition on the device\n");
    printf("\n");
    printf("The option --live-dangerously may be passed in front of any command\n");
    printf("to skip the write confirmation prompt.\n");

    return status_to_retcode(ret);
}

int cgetc(void) {
    uint8_t ch;
    for (;;) {
        ssize_t r = read(0, &ch, 1);
        if (r < 0)
            return static_cast<int>(r);
        if (r == 1)
            return ch;
    }
}

struct guid {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
};

char* guid_to_cstring(char* dst, const uint8_t* src) {
    struct guid* guid = (struct guid*)src;
    sprintf(dst, "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X", guid->data1, guid->data2, guid->data3, guid->data4[0], guid->data4[1], guid->data4[2], guid->data4[3], guid->data4[4], guid->data4[5], guid->data4[6], guid->data4[7]);
    return dst;
}

char* cros_flags_to_cstring(char* dst, size_t dst_len, uint64_t flags) {
    uint32_t priority = gpt_cros_attr_get_priority(flags);
    uint32_t tries = gpt_cros_attr_get_tries(flags);
    bool successful = gpt_cros_attr_get_successful(flags);
    snprintf(dst, dst_len, "priority=%u tries=%u successful=%u", priority, tries, successful);
    dst[dst_len-1] = 0;
    return dst;
}

char* flags_to_cstring(char* dst, size_t dst_len, const uint8_t* guid, uint64_t flags) {
    if (gpt_cros_is_kernel_guid(guid, sizeof(struct guid))) {
        return cros_flags_to_cstring(dst, dst_len, flags);
    } else {
        snprintf(dst, dst_len, "0x%016" PRIx64, flags);
    }
    dst[dst_len-1] = 0;
    return dst;
}

fbl::unique_ptr<GptDevice> init(const char* dev) {
    fbl::unique_fd fd(open(dev, O_RDWR));
    if (!fd.is_valid()) {
        printf("error opening %s\n", dev);
        return nullptr;
    }

    block_info_t info;
    ssize_t rc = ioctl_block_get_info(fd.get(), &info);
    if (rc < 0) {
        printf("error getting block info\n");
        return nullptr;
    }

    printf("blocksize=0x%X blocks=%" PRIu64 "\n", info.block_size, info.block_count);

    fbl::unique_ptr<GptDevice> gpt;
    rc = GptDevice::Create(fd.get(), info.block_size, info.block_count, &gpt);
    if (rc < 0) {
        printf("error initializing GPT\n");
        return fbl::unique_ptr<GptDevice>(nullptr);
    }

    return gpt;
}

constexpr void setxy(unsigned yes, const char** X, const char** Y) {
    if (yes) {
        *X = "\033[7m";
        *Y = "\033[0m";
    } else {
        *X = "";
        *Y = "";
    }
}

void dump(const GptDevice* gpt, int* count) {
    if (!gpt->Valid()) {
        return;
    }
    const gpt_partition_t* p;
    char name[gpt::kGuidStrLength];
    char guid[gpt::kGuidStrLength];
    char id[gpt::kGuidStrLength];
    char flags_str[256];
    const char* X;
    const char* Y;
    uint32_t i;
    for (i = 0; i < gpt::kPartitionCount; i++) {
        p = gpt->GetPartition(i);
        if (!p) break;
        memset(name, 0, gpt::kGuidStrLength);
        unsigned diff;
        ZX_ASSERT(gpt->GetDiffs(i, &diff) == ZX_OK);
        setxy(diff & gpt::kGptDiffName, &X, &Y);
        printf("Partition %d: %s%s%s\n",
               i, X, utf16_to_cstring(name, (const uint16_t*)p->name, gpt::kGuidStrLength - 1), Y);
        setxy(diff & (gpt::kGptDiffFirst | gpt::kGptDiffLast), &X, &Y);
        printf("    Start: %s%" PRIu64 "%s, End: %s%" PRIu64 "%s (%" PRIu64 " blocks)\n",
               X, p->first, Y, X, p->last, Y, p->last - p->first + 1);
        setxy(diff & gpt::kGptDiffGuid, &X, &Y);
        printf("    id:   %s%s%s\n", X, guid_to_cstring(guid, (const uint8_t*)p->guid), Y);
        setxy(diff & gpt::kGptDiffType, &X, &Y);
        printf("    type: %s%s%s\n", X, guid_to_cstring(id, (const uint8_t*)p->type), Y);
        setxy(diff & gpt::kGptDiffName, &X, &Y);
        printf("    flags: %s%s%s\n", X, flags_to_cstring(flags_str, sizeof(flags_str), p->type, p->flags), Y);
    }
    if (count) {
        *count = i;
    }
}

void dump_partitions(const char* dev) {
    fbl::unique_ptr<GptDevice> gpt = init(dev);
    if (!gpt) return;

    if (!gpt->Valid()) {
        printf("No valid GPT found\n");
        return;
    }

    printf("Partition table is valid\n");

    uint64_t start, end;
    if (gpt->Range(&start, &end) != ZX_OK) {
        printf("Couldn't identify device range\n");
        return;
    }

    printf("GPT contains usable blocks from %" PRIu64 " to %" PRIu64" (inclusive)\n", start, end);
    int count;
    dump(gpt.get(), &count);
    printf("Total: %d partitions\n", count);
}

bool ConfirmCommit(const GptDevice* gpt, const char* dev) {
    if (confirm_writes) {
        dump(gpt, NULL);
        printf("\n");
        printf("WARNING: About to write partition table to: %s\n", dev);
        printf("WARNING: Type 'y' to continue, 'n' or ESC to cancel\n");

        for (;;) {
            switch (cgetc()) {
            case 'y':
            case 'Y':
                return true;
            case 'n':
            case 'N':
            case 27:
                return false;
            }
        }
    }

    return true;
}

zx_status_t commit(GptDevice* gpt, const char* dev) {

    if (!ConfirmCommit(gpt, dev)) {
        return ZX_OK;
    }

    zx_status_t rc = gpt->Sync();
    if (rc != ZX_OK) {
        printf("Error: GPT device sync failed.\n");
        return rc;
    }
    if ((rc = gpt->BlockRrPart()) != ZX_OK) {
        printf("Error: GPT updated but device could not be rebound. Please reboot.\n");
        return rc;
    }
    printf("GPT changes complete.\n");
    return ZX_OK;
}

void init_gpt(const char* dev) {
    fbl::unique_ptr<GptDevice> gpt = init(dev);
    if (!gpt) return;

    // generate a default header
    ZX_ASSERT(gpt->RemoveAllPartitions() == ZX_OK);
    commit(gpt.get(), dev);
}

void add_partition(const char* dev, uint64_t start, uint64_t end, const char* name) {
    uint8_t guid[GPT_GUID_LEN];
    zx_cprng_draw(guid, GPT_GUID_LEN);

    fbl::unique_ptr<GptDevice> gpt = init(dev);
    if (!gpt) return;

    if (!gpt->Valid()) {
        // generate a default header
        if (commit(gpt.get(), dev)) {
            return;
        }
    }

    uint8_t type[GPT_GUID_LEN];
    memset(type, 0xff, GPT_GUID_LEN);
    zx_status_t rc = gpt->AddPartition(name, type, guid, start, end - start + 1, 0);
    if (rc == ZX_OK) {
        printf("add partition: name=%s start=%" PRIu64 " end=%" PRIu64 "\n", name, start, end);
        commit(gpt.get(), dev);
    }
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
bool parse_guid(const char* guid, uint8_t* bytes_out) {
    if (strlen(guid) != gpt::kGuidStrLength - 1) {
        printf("GUID length is wrong: %zd but expected %" PRIu64 "\n", strlen(guid),
               (gpt::kGuidStrLength - 1));
        return false;
    }

    // how many nibbles of the byte we've processed
    uint8_t nibbles = 0;
    // value to accumulate byte as we parse its two char nibbles
    uint8_t val = 0;
    // which byte we're parsing
    uint8_t out_idx = 0;
    uint8_t dashes = 0;

    for (uint64_t idx = 0; idx < gpt::kGuidStrLength - 1; idx++) {
        char c = guid[idx];

        uint8_t char_val = 0;
        if (c == '-') {
            dashes++;
            continue;
        } else if (c >= '0' && c <= '9') {
            char_val = static_cast<uint8_t>(c - '0');
        } else if (c >= 'A' && c <= 'F') {
            char_val = static_cast<uint8_t>(c - (uint8_t)'A' + (uint8_t)10);
        } else if (c >= 'a' && c <= 'f') {
            char_val = static_cast<uint8_t>(c - 'a' + 10);
        } else {
            fprintf(stderr, "'%c' is not a valid GUID character\n", c);
            return false;
        }

        val = static_cast<uint8_t>(val + (char_val << (4 * (1 - nibbles))));

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
 * bounds and other error checking. If ZX_OK is returned, the out parameters
 * will be set to valid values.
 */
zx_status_t get_gpt_and_part(const char* path_device, uint32_t idx_part,
                             fbl::unique_ptr<GptDevice>* gpt_out,
                             gpt_partition_t** part_out) {
    if (idx_part >= gpt::kPartitionCount) {
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::unique_ptr<GptDevice> gpt = init(path_device);
    if (gpt == NULL) {
        return ZX_ERR_INTERNAL;
    }

    gpt_partition_t* part = gpt->GetPartition(idx_part);
    if (part == NULL) {
        return ZX_ERR_INTERNAL;
    }

    *gpt_out = std::move(gpt);
    *part_out = part;
    return ZX_OK;
}

constexpr struct {
    const char* name;
    const uint8_t guid[GPT_GUID_LEN];
} nametab[] = {
    { .name = "blobfs", .guid = GUID_BLOB_VALUE, },
    { .name = "data", .guid = GUID_DATA_VALUE, },
    { .name = "install", .guid = GUID_INSTALL_VALUE, },
    { .name = "system", .guid = GUID_SYSTEM_VALUE, },
    { .name = "efi", .guid = GUID_EFI_VALUE, },
    { .name = "zircon-a", .guid = GUID_ZIRCON_A_VALUE, },
    { .name = "zircon-b", .guid = GUID_ZIRCON_B_VALUE, },
    { .name = "zircon-r", .guid = GUID_ZIRCON_R_VALUE, },
};

/*
 * Match keywords "BLOBFS", "DATA", "SYSTEM", or "EFI" and convert them to their
 * corresponding byte sequences. 'out' should point to a GPT_GUID_LEN array.
 */
constexpr bool expand_special(const char* in, uint8_t* out) {
    if (in == NULL) {
        return false;
    }

    for (unsigned n = 0; n < countof(nametab); n++) {
        if (!strcmp(in, nametab[n].name)) {
            memcpy(out, nametab[n].guid, GPT_GUID_LEN);
            return true;
        }
    }

    return false;
}

void remove_partition(const char* dev, uint32_t n) {
    fbl::unique_ptr<GptDevice> gpt = init(dev);
    if (!gpt) return;

    if (n >= gpt::kPartitionCount) {
        return;
    }
    gpt_partition_t* p = gpt->GetPartition(n);
    if (!p) {
        return;
    }
    if (gpt->RemovePartition(p->guid) != ZX_OK) {
        return;
    }
    char name[gpt::kGuidStrLength];
    printf("remove partition: n=%u name=%s\n", n,
           utf16_to_cstring(name, (const uint16_t*)p->name, gpt::kGuidStrLength - 1));
    commit(gpt.get(), dev);
}

zx_status_t adjust_partition(const char* dev, uint32_t idx_part,
                             uint64_t start, uint64_t end) {
    fbl::unique_ptr<GptDevice> gpt = NULL;
    gpt_partition_t* part = NULL;

    if (end < start) {
        fprintf(stderr, "partition #%u would end before it started\n", idx_part);
    }

    zx_status_t rc = get_gpt_and_part(dev, idx_part, &gpt, &part);
    if (rc != ZX_OK) {
        return rc;
    }

    uint64_t block_start, block_end;
    if ((rc = gpt->Range(&block_start, &block_end)) != ZX_OK) {
        return rc;
    }

    if ((start < block_start) || (end > block_end)) {
        fprintf(stderr, "partition #%u would be outside of valid block range\n", idx_part);
        return ZX_ERR_OUT_OF_RANGE;
    }

    for (uint32_t idx = 0; idx < gpt::kPartitionCount; idx++) {
        // skip this partition and non-existent partitions
        if ((idx == idx_part) || (gpt->GetPartition(idx) == NULL)) {
            continue;
        }
        // skip partitions we don't intersect
        if ((start > gpt->GetPartition(idx)->last) ||
            (end < gpt->GetPartition(idx)->first)) {
            continue;
        }
        fprintf(stderr, "partition #%u would overlap partition #%u\n", idx_part, idx);
        return ZX_ERR_UNAVAILABLE;
    }

    part->first = start;
    part->last = end;

    return commit(gpt.get(), dev);
}

/*
 * Edit a partition, changing either its type or ID GUID. path_device should be
 * the path to the device where the GPT can be read. idx_part should be the
 * index of the partition in the GPT that you want to change. guid should be the
 * string/human-readable form of the GUID and should be 36 characters plus a
 * null terminator.
 */
zx_status_t edit_partition(const char* dev, uint32_t idx_part,
                           char* type_or_id, char* guid) {
    fbl::unique_ptr<GptDevice> gpt = NULL;
    gpt_partition_t* part = NULL;

    // whether we're setting the type or id GUID
    bool set_type;

    if (!strcmp(type_or_id, "type")) {
        set_type = true;
    } else if (!strcmp(type_or_id, "id")) {
        set_type = false;
    } else {
        return ZX_ERR_INVALID_ARGS;
    }

    uint8_t guid_bytes[GPT_GUID_LEN];
    if (!expand_special(guid, guid_bytes) && !parse_guid(guid, guid_bytes)) {
        printf("GUID could not be parsed.\n");
        return ZX_ERR_INVALID_ARGS;
    }

    zx_status_t rc = get_gpt_and_part(dev, idx_part, &gpt, &part);
    if (rc != ZX_OK) {
        return rc;
    }

    if (set_type) {
        memcpy(part->type, guid_bytes, GPT_GUID_LEN);
    } else {
        memcpy(part->guid, guid_bytes, GPT_GUID_LEN);
    }

    return commit(gpt.get(), dev);
}

/*
 * Edit a Chrome OS kernel partition, changing its attributes.
 *
 * argv/argc should correspond only to the arguments after the command.
 */
int edit_cros_partition(char* const* argv, int argc) {
    fbl::unique_ptr<GptDevice> gpt = NULL;
    gpt_partition_t* part = NULL;
    zx_status_t rc;
    char* dev;

    char* end;
    unsigned long lidx_part = strtoul(argv[0], &end, 10);
    if (*end != 0 || argv[0][0] == 0 || lidx_part > UINT32_MAX) {
        return usage(ZX_ERR_INVALID_ARGS);
    }
    uint32_t idx_part = static_cast<uint32_t>(lidx_part);

    // Use -1 as a sentinel for "not changing"
    long tries = -1;
    long priority = -1;
    int successful = -1;

    int c;
    while ((c = getopt(argc, argv, "T:P:S:")) > 0) {
        switch (c) {
        case 'T': {
            long val = strtol(optarg, &end, 10);
            if (*end != 0 || optarg[0] == 0) {
                return usage(ZX_ERR_INVALID_ARGS);
            }
            if (val < 0 || val > 15) {
                printf("tries must be in the range [0, 16)\n");
                return usage(ZX_ERR_INVALID_ARGS);
            }
            tries = val;
            break;
        }
        case 'P': {
            long val = strtol(optarg, &end, 10);
            if (*end != 0 || optarg[0] == 0) {
                return usage(ZX_ERR_INVALID_ARGS);
            }
            if (val < 0 || val > 15) {
                printf("priority must be in the range [0, 16)\n");
                return usage(ZX_ERR_INVALID_ARGS);
            }
            priority = val;
            break;
        }
        case 'S': {
            if (!strncmp(optarg, "0", 2)) {
                successful = 0;
            } else if (!strncmp(optarg, "1", 2)) {
                successful = 1;
            } else {
                printf("successful must be 0 or 1\n");
                return usage(ZX_ERR_INVALID_ARGS);
            }
            break;
        }
        default:
            printf("Unknown option\n");
            return usage(ZX_ERR_INVALID_ARGS);
        }
    }

    if (optind != argc - 1) {
        printf("Did not specify device arg\n");
        return usage(ZX_ERR_INVALID_ARGS);
    }

    dev = argv[optind];

    rc = get_gpt_and_part(dev, idx_part, &gpt, &part);
    if (rc != ZX_OK) {
        return status_to_retcode(rc);
    }

    if (!gpt_cros_is_kernel_guid(part->type, GPT_GUID_LEN)) {
        printf("Partition is not a CrOS kernel partition\n");
        return status_to_retcode(ZX_ERR_INVALID_ARGS);
    }

    if (tries >= 0) {
        if (gpt_cros_attr_set_tries(&part->flags, static_cast<uint8_t>(tries)) < 0) {
            printf("Failed to set tries\n");
            return status_to_retcode(ZX_ERR_INVALID_ARGS);
        }
    }
    if (priority >= 0) {
        if (gpt_cros_attr_set_priority(&part->flags, static_cast<uint8_t>(priority)) < 0) {
            printf("Failed to set priority\n");
            return status_to_retcode(ZX_ERR_INVALID_ARGS);
        }
    }
    if (successful >= 0) {
        gpt_cros_attr_set_successful(&part->flags, successful);
    }

    return status_to_retcode(commit(gpt.get(), dev));
}

/*
 * Set whether a partition is visible or not to the EFI firmware. If a
 * partition is set as hidden, the firmware will not attempt to boot from the
 * partition.
 */
zx_status_t set_visibility(char* dev, uint32_t idx_part, bool visible) {
    fbl::unique_ptr<GptDevice> gpt = NULL;
    gpt_partition_t* part = NULL;

    zx_status_t rc = get_gpt_and_part(dev, idx_part, &gpt, &part);
    if (rc != ZX_OK) {
        return rc;
    }

    if (visible) {
        part->flags &= ~kFlagHidden;
    } else {
        part->flags |= kFlagHidden;
    }

    return commit(gpt.get(), dev);
}

// parse_size parses long integers in base 10, expanding p, t, g, m, and k
// suffices as binary byte scales. If the suffix is %, the value is returned as
// negative, in order to indicate a proportion.
int64_t parse_size(char *s) {
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
      __FALLTHROUGH;
    case 't':
    case 'T':
      v *= 1024;
      __FALLTHROUGH;
    case 'g':
    case 'G':
      v *= 1024;
      __FALLTHROUGH;
    case 'm':
    case 'M':
      v *= 1024;
      __FALLTHROUGH;
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
uint64_t align(uint64_t base, uint64_t logical, uint64_t physical) {
  uint64_t a = logical;
  if (physical > a) a = physical;
  uint64_t base_bytes = base * logical;
  uint64_t d = base_bytes % a;
  return (base_bytes + a - d) / logical;
}

// repartition expects argv to start with the disk path and be followed by
// triples of name, type and size.
zx_status_t repartition(int argc, char** argv) {
  const char* dev = argv[0];
  uint64_t logical, free_space;
  fbl::unique_ptr<GptDevice> gpt = init(dev);
  if (gpt == NULL) {
    return ZX_ERR_INTERNAL;
  }

  argc--;
  argv = &argv[1];
  int num_partitions = argc/3;

  gpt_partition_t* p = gpt->GetPartition(0);
  while (p) {
      ZX_ASSERT(gpt->RemovePartition(p->guid) == ZX_OK);
      p = gpt->GetPartition(0);
  }

  logical = gpt->BlockSize();
  free_space = gpt->TotalBlockCount() * logical;

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
            return ZX_ERR_INVALID_ARGS;
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
    ZX_ASSERT(gpt->Range(&first_usable, &last_usable) == ZX_OK);

    uint64_t start = align(first_usable, logical, physical);

    for (int i = 0; i < num_partitions; i++) {
      char *name = argv[i*3];
      char *type_string = argv[i*3+1];

      uint64_t byte_size = sizes[i];

      uint8_t type[GPT_GUID_LEN];
      if (!expand_special(type_string, type) && !parse_guid(type_string, type)) {
          printf("GUID could not be parsed: %s\n", type_string);
          return ZX_ERR_INVALID_ARGS;
      }

      uint8_t guid[GPT_GUID_LEN];
      zx_cprng_draw(guid, GPT_GUID_LEN);

      // end is clamped to the sector before the next aligned partition, in order
      // to avoid wasting alignment space at the tail of partitions.
      uint64_t nblocks = (byte_size/logical) + (byte_size%logical == 0 ? 0 : 1);
      uint64_t end = align(start+nblocks+1, logical, physical) - 1;
      if (end > last_usable) end = last_usable;

      if (start > last_usable) {
        printf("partition %s does not fit\n", name);
        return ZX_ERR_OUT_OF_RANGE;
      }

      printf("%s: %" PRIu64 " bytes, %" PRIu64 " blocks, %" PRIu64 "-%" PRIu64 "\n",
             name, byte_size, nblocks, start, end);
      ZX_ASSERT(gpt->AddPartition(name, type, guid, start, end - start, 0) == ZX_OK);

      start = end + 1;
    }
  }

  return commit(gpt.get(), dev);
}

} // namespace

int main(int argc, char** argv) {
    bin_name = argv[0];
    const char* cmd;
    uint32_t idx_part;

    if (argc > 1) {
        if (!strcmp(argv[1], "--live-dangerously")) {
            confirm_writes = false;
            argc--;
            argv++;
        }
    }

    if (argc == 1) {
        return usage(ZX_OK);
    }

    cmd = argv[1];
    if (!strcmp(cmd, "dump")) {
        if (argc <= 2) {
            return usage(ZX_OK);
        }
        dump_partitions(argv[2]);
    } else if (!strcmp(cmd, "init")) {
        if (argc <= 2) {
            return usage(ZX_OK);
        }
        init_gpt(argv[2]);
    } else if (!strcmp(cmd, "add")) {
        if (argc <= 5) {
            return usage(ZX_OK);
        }
        add_partition(argv[5], strtoull(argv[2], NULL, 0), strtoull(argv[3], NULL, 0), argv[4]);
    } else if (!strcmp(cmd, "remove")) {
        if (argc <= 3) {
            return usage(ZX_OK);
        }
        if (ReadPartitionIndex(argv[2], &idx_part) != ZX_OK) {
            return usage(ZX_OK);
        }
        remove_partition(argv[3], idx_part);
    } else if (!strcmp(cmd, "edit")) {
        if (argc <= 5) {
            return usage(ZX_OK);
        }
        if (ReadPartitionIndex(argv[2], &idx_part) != ZX_OK) {
            return usage(ZX_OK);
        }
        if (edit_partition(argv[5], idx_part, argv[3], argv[4])) {
            printf("failed to edit partition.\n");
        }
    } else if (!strcmp(cmd, "edit_cros")) {
        if (argc <= 4) {
            return usage(ZX_OK);
        }
        if (edit_cros_partition(argv + 2, argc - 2)) {
            printf("failed to edit partition.\n");
        }
    } else if (!strcmp(cmd, "adjust")) {
        if (argc <= 5) {
            return usage(ZX_OK);
        }
        if (ReadPartitionIndex(argv[2], &idx_part) != ZX_OK) {
            return usage(ZX_OK);
        }
        if (adjust_partition(argv[5], idx_part,
                             strtoull(argv[3], NULL, 0), strtoull(argv[4], NULL, 0)) != ZX_OK) {
            printf("failed to adjust partition.\n");
        }
    } else if (!strcmp(cmd, "visible")) {
        if (argc < 5) {
            return usage(ZX_OK);
        }
        bool visible;
        if (!strcmp(argv[3], "true")) {
            visible = true;
        } else if (!strcmp(argv[3], "false")) {
            visible = false;
        } else {
            return usage(ZX_OK);
        }

        if (ReadPartitionIndex(argv[2], &idx_part) != ZX_OK) {
            return usage(ZX_OK);
        }
        if (set_visibility(argv[4], idx_part, visible) != ZX_OK) {
            printf("Error changing visibility.\n");
        }
    } else if (!strcmp(cmd, "repartition")) {
        if (argc < 6) {
            return usage(ZX_OK);
        }
        if (argc % 3 != 0) {
            return usage(ZX_OK);
        }
        return status_to_retcode(repartition(argc - 2, &argv[2]));
    } else {
        return usage(ZX_OK);
    }

    return 0;
}
