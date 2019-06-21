// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <lib/zircon-internal/ktrace.h>

constexpr uint32_t kChunkSize = 65536;

typedef enum { Tag16B, Tag32B, TagNAME } TagType;

typedef struct {
    uint32_t num;
    uint32_t group;
    TagType type;
    const char* name;
} TagInfo;

static const TagInfo g_tags[] = {
#define KTRACE_DEF(num, type, name, group) \
  [num] = {num, KTRACE_GRP_ ## group, Tag ## type, #name},
#include <lib/zircon-internal/ktrace-def.h>
};

static const char kUsage[] = "\
Usage: ktrace-pretty-print <path>\n\
       ktrace-pretty-print --help\n\
";

static uint8_t buffer[kChunkSize];

// Where to read from next.
static uint8_t* current = buffer;

// Limit of data read into |buffer|.
static uint8_t* marker = buffer;

static uint8_t* const end = buffer + kChunkSize;

static size_t number_records_read = 0;
static size_t number_bytes_read = 0;

static inline size_t AvailableBytes() {
    assert(marker >= current);
    return marker - current;
}

static void PrintUsage(FILE* f) {
    fputs(kUsage, f);
}

static void ReadMoreData(int fd) {
    memcpy(buffer, current, AvailableBytes());
    marker = buffer + AvailableBytes();

    while (marker < end) {
        ssize_t bytes_read = read(fd, marker, end - marker);
        if (bytes_read <= 0)
            break;

        marker += bytes_read;
    }

    current = buffer;
}

// no-inline for easier debugging in gdb
__NO_INLINE static ktrace_header_t* ReadNextRecord(int fd) {
    if (AvailableBytes() < sizeof(ktrace_header_t))
        ReadMoreData(fd);

    if (AvailableBytes() < sizeof(ktrace_header_t))
        return nullptr;

    ktrace_header_t* record = (ktrace_header_t*) current;

    if (AvailableBytes() < KTRACE_LEN(record->tag))
        ReadMoreData(fd);

    if (AvailableBytes() < KTRACE_LEN(record->tag))
        return nullptr;

    record = (ktrace_header_t*) current;

    // If the record has zero length we're hosed.
    if (KTRACE_LEN(record->tag) == 0) {
        printf("Zero length tag, done.\n");
        return nullptr;
    }

    current += KTRACE_LEN(record->tag);

    number_bytes_read += KTRACE_LEN(record->tag);
    number_records_read += 1;
    return record;
}

static void PrintTag(uint32_t tag) {
    uint32_t event = KTRACE_EVENT(tag);
    uint32_t flags = KTRACE_FLAGS(tag);
    const TagInfo* info = &g_tags[event];
    assert(info->name != nullptr);
    printf("%s(0x%x)", info->name, event);
    if (flags != 0) {
        printf(", flags 0x%x", flags);
    }
}

static void Dump16B(const TagInfo* info, ktrace_header_t* r) {
    printf("%" PRIu64 ": ", r->ts);
    PrintTag(r->tag);
    // TODO(dje): Further decode args.
    printf(", arg 0x%x\n", r->tid);
}

static void Dump32B(const TagInfo* info, ktrace_rec_32b_t* r) {
    printf("%" PRIu64 ": ", r->ts);
    PrintTag(r->tag);
    // TODO(dje): Further decode args.
    printf(", tid 0x%x, a 0x%x, b 0x%x, c 0x%x, d 0x%x\n",
           r->tid, r->a, r->b, r->c, r->d);
}

static void DumpName(const TagInfo* info, ktrace_rec_name_t* r) {
    PrintTag(r->tag);
    printf(", id 0x%x, arg 0x%x, %s\n", r->id, r->arg, r->name);
}

static int DoDump(const fbl::unique_fd& fd) {
    ktrace_header_t* record;

    while ((record = ReadNextRecord(fd.get())) != nullptr) {
        uint32_t event = KTRACE_EVENT(record->tag);
        if (event >= countof(g_tags)) {
            printf("Unexpected event: 0x%x\n", event);
            continue;
        }
        const TagInfo* info = &g_tags[event];
        if (info->name == nullptr) {
            printf("Unexpected event: 0x%x\n", event);
            continue;
        }
        switch (info->type) {
        case Tag16B:
            Dump16B(info, record);
            break;
        case Tag32B:
            Dump32B(info, (ktrace_rec_32b_t*) record);
            break;
        case TagNAME:
            DumpName(info, (ktrace_rec_name_t*) record);
            break;
        default:
            printf("Unexpected tag type: 0x%x\n", info->type);
            break;
        }
    }

    printf("%zu records, %zu bytes\n", number_records_read, number_bytes_read);
    return EXIT_SUCCESS;
}

int main(int argc, char* argv[]) {
    if (argc >= 2 && strcmp(argv[1], "--help") == 0) {
        PrintUsage(stdout);
        return EXIT_SUCCESS;
    }

    if (argc != 2) {
        PrintUsage(stderr);
        return EXIT_FAILURE;
    }
    const char* path = argv[1];

    fbl::unique_fd fd(open(path, O_RDONLY));
    if (!fd.is_valid()) {
        fprintf(stderr, "Unable to open file for reading: %s\n", path);
        return EXIT_FAILURE;
    }
    return DoDump(fd);
}
