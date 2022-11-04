// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/performance/ktrace_provider/importer.h"

#include <lib/fxt/fields.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zircon-internal/ktrace.h>
#include <lib/zx/clock.h>
#include <zircon/syscalls.h>

#include <iomanip>
#include <iterator>
#include <string_view>

#include <fbl/algorithm.h>
#include <fbl/string_printf.h>

#include "src/performance/ktrace_provider/reader.h"

namespace ktrace_provider {

#define MAKE_STRING(literal) trace_context_make_registered_string_literal(context_, literal)

Importer::Importer(trace_context_t* context) : context_(context) {}

#undef MAKE_STRING

Importer::~Importer() = default;

bool Importer::Import(Reader& reader) {
  auto start = zx::clock::get_monotonic();

  while (true) {
    if (auto record = reader.ReadNextRecord()) {
      // A record with a group bitfield of 0 is a padding record.  It contains
      // no info, and is just used to pad the kernel's ring buffer to maintain
      // continuity when need.  Skip it.
      if (KTRACE_GROUP(record->tag) == 0) {
        FX_VLOGS(5) << "Skipped ktrace padding record, tag=0x" << std::hex << record->tag;
        continue;
      }

      if (KTRACE_GROUP(record->tag) & KTRACE_GRP_FXT) {
        const size_t fxt_record_size = KTRACE_LEN(record->tag) - sizeof(uint64_t);
        const uint64_t* fxt_record = reinterpret_cast<const uint64_t*>(record) + 1;

        // Verify that the FXT record header specifies the correct size.
        const size_t fxt_size_from_header =
            fxt::RecordFields::RecordSize::Get<size_t>(fxt_record[0]) * sizeof(uint64_t);
        if (fxt_size_from_header != fxt_record_size) {
          FX_LOGS(ERROR) << "Found fxt record of size " << fxt_record_size
                         << " bytes whose header indicates a record of size "
                         << fxt_size_from_header << " bytes. Skipping.";
          continue;
        }

        void* dst = trace_context_alloc_record(context_, fxt_record_size);
        if (dst != nullptr) {
          memcpy(dst, reinterpret_cast<const char*>(fxt_record), fxt_record_size);
        }

        continue;
      }

      FX_LOGS(WARNING) << "Found record that is expected to be migrated to FXT.";
    } else {
      break;
    }
  }

  size_t nr_bytes_read = reader.number_bytes_read();
  size_t nr_records_read = reader.number_records_read();

  // This is an INFO and not VLOG() as we currently always want to see this.
  FX_LOGS(INFO) << "Import of " << nr_records_read << " ktrace records"
                << "(" << nr_bytes_read
                << " bytes) took: " << (zx::clock::get_monotonic() - start).to_usecs() << "us";

  return true;
}

}  // namespace ktrace_provider
