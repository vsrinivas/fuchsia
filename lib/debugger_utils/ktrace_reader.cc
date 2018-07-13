// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Simple reader for ktrace files.
// TODO: IWBN if there was a libktrace to replace this.

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <zircon/ktrace.h>

#include "ktrace_reader.h"

namespace debugger_utils {

int KtraceReadFile(int fd, KtraceRecordReader* reader, void* arg) {
  KtraceRecord rec;
  unsigned offset = 0;

  while (read(fd, rec.raw, sizeof(ktrace_header_t)) ==
         sizeof(ktrace_header_t)) {
    uint32_t tag = rec.hdr.tag;
    uint32_t len = KTRACE_LEN(tag);
    if (tag == 0) {
      fprintf(stderr, "eof: zero tag at offset %08x\n", offset);
      break;
    }
    if (len < sizeof(ktrace_header_t)) {
      fprintf(stderr, "eof: short packet at offset %08x\n", offset);
      break;
    }
    offset += (sizeof(ktrace_header_t) + len);
    len -= sizeof(ktrace_header_t);
    if (read(fd, rec.raw + sizeof(ktrace_header_t), len) != len) {
      fprintf(stderr, "eof: incomplete packet at offset %08x\n", offset);
      break;
    }

    int rc = reader(&rec, arg);
    if (rc)
      return rc;
  }

  return 0;
}

const char* KtraceRecName(uint32_t tag) {
  // TODO: Remove magic number
  switch (tag & 0xffffff00u) {
#define KTRACE_DEF(num, type, name, group)     \
  case KTRACE_TAG(num, KTRACE_GRP_##group, 0): \
    return #name;
#include <zircon/ktrace-def.h>
    default:
      return "UNKNOWN";
  }
}

}  // namespace debugger_utils

#ifdef TEST

struct TestData {
  size_t count;
};

static void Dump_16B(const ktrace_rec_32b_t* rec) {
  printf(" tid 0x%x, ts 0x%" PRIx64, rec->tid, rec->ts);
  printf(" a 0x%x, b 0x%x", rec->a, rec->b);
}

static void Dump_32B(const ktrace_rec_32b_t* rec) {
  printf(" tid 0x%x, ts 0x%" PRIx64, rec->tid, rec->ts);
  printf(" a 0x%x, b 0x%x, c 0x%x, d 0x%x", rec->a, rec->b, rec->c, rec->d);
}

static void Dump_NAME(const ktrace_rec_name_t* rec) {
  printf(" id 0x%x, arg 0x%x,", rec->id, rec->arg);
  printf(" name %s", rec->name);
}

static void DumpRecord(const debugger_utils::KtraceRecord* rec) {
  printf("%s(%x):", debugger_utils::KtraceRecName(rec->hdr.tag), rec->hdr.tag);

  // TODO: Remove magic number
  switch (rec->hdr.tag & 0xffffff00u) {
#define KTRACE_DEF(num, type, name, group)     \
  case KTRACE_TAG(num, KTRACE_GRP_##group, 0): \
    Dump_##type(&rec->r_##type);               \
    break;
#include <zircon/ktrace-def.h>
    default:
      printf(" ???");
      break;
  }

  printf("\n");
}

static int ProcessTest(debugger_utils::KtraceRecord* rec, void* arg) {
  auto data = reinterpret_cast<TestData*>(arg);

  ++data->count;

  DumpRecord(rec);

  return 0;
}

int main(int argc, char* argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: ktrace-reader <file>\n");
    exit(1);
  }

  const char* file = argv[1];
  int fd = open(file, O_RDONLY);
  if (fd < 0) {
    fprintf(stderr, "Error opening: %s: %s\n", file, strerror(errno));
    exit(1);
  }

  TestData data = {};

  int rc = debugger_utils::KtraceReadFile(fd, ProcessTest, &data);

  printf("process_test returned %d\n", rc);
  printf("%zu records\n", data.count);

  return 0;
}

#endif
