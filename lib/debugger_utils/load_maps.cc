// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "load_maps.h"

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

#include "lib/fxl/files/directory.h"
#include "lib/fxl/files/path.h"
#include "lib/fxl/functional/auto_call.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

#include "util.h"

namespace debugger_utils {

bool LoadMapTable::ReadLogListenerOutput(const std::string& file) {
  FXL_LOG(INFO) << "Loading load maps from " << file;

  FILE* f = fopen(file.c_str(), "r");
  if (!f) {
    FXL_LOG(ERROR) << "error opening file, " << ErrnoString(errno);
    return false;
  }
  auto close_file = fxl::MakeAutoCall([&]() { fclose(f); });

  constexpr size_t kMaxLineLen = 1024;
  char* line = nullptr;
  size_t line_capacity = 0;
  int lineno = 1;

  std::map<uint64_t, LoadMap> map_data;
  ssize_t line_len;

  // These buffers are needed to break out elements of the line. Rather than
  // allocate space for them during each iteration we allocate them here.
  // And rather than use up a fair bit of stack, we use the heap.
  char* prefix = reinterpret_cast<char*>(malloc(kMaxLineLen));
  char* build_id = reinterpret_cast<char*>(malloc(kMaxLineLen));
  char* name = reinterpret_cast<char*>(malloc(kMaxLineLen));
  char* so_name = reinterpret_cast<char*>(malloc(kMaxLineLen));

  auto free_mem = fxl::MakeAutoCall([&]() {
    free(line);
    free(prefix);
    free(build_id);
    free(name);
    free(so_name);
  });

  if (!prefix || !build_id || !name || !so_name) {
    FXL_LOG(ERROR) << "Out of memory";
    return false;
  }

  for (; (line_len = getline(&line, &line_capacity, f)) > 0; ++lineno) {
    // For paranoia's sake, watch for embedded NULs.
    size_t n = strlen(line);
    if (static_cast<ssize_t>(n) != line_len) {
      FXL_LOG(WARNING) << "Line contains embedded NULs: "
                       << std::string(line, line_len);
      continue;
    }
    if (n > 0 && line[n - 1] == '\n')
      line[n - 1] = '\0';
    FXL_VLOG(2) << fxl::StringPrintf("%d: %s", lineno, line);

    if (n > kMaxLineLen) {
      FXL_VLOG(2) << fxl::StringPrintf("%d: too long, ignoring", lineno);
    }

    if (!strcmp(line, "\n"))
      continue;
    if (line[0] == '#')
      continue;

    // If this is a new boot, start over.
    if (strstr(line, "welcome to lk/MP")) {
      FXL_VLOG(1) << "Restarting reading of load maps, machine rebooted";
      map_data.clear();
      Clear();
      continue;
    }

    // The sequence number is used for grouping records, done beforehand, but
    // is no longer needed after that.
    unsigned seqno;
    uint64_t pid, base_addr, load_addr, end_addr;

    // ld.so dumps the data in three separate records to avoid line-wrapping:
    // a: base load end
    // b: build_id
    // c: name so_name
    // TODO(dje): See MG-519. This is a temp hack until ld.so logs this data
    // via something better.

#define GET_ENTRY_ID(pid, seqno) (((pid) << 8) + (seqno))

    if (sscanf(line,
               "%[^@]@trace_load: %" PRIu64 ":%ua"
               " 0x%" PRIx64 " 0x%" PRIx64 " 0x%" PRIx64,
               prefix, &pid, &seqno, &base_addr, &load_addr, &end_addr) == 6) {
      uint64_t id = GET_ENTRY_ID(pid, seqno);
      if (map_data.find(id) != map_data.end()) {
        FXL_LOG(ERROR) << "Already have map entry for: " << line;
        continue;
      }
      struct LoadMap entry;
      entry.pid = pid;
      entry.base_addr = base_addr;
      entry.load_addr = load_addr;
      entry.end_addr = end_addr;
      map_data[id] = entry;
    } else if (sscanf(line,
                      "%[^@]@trace_load: %" PRIu64 ":%ub"
                      " %s",
                      prefix, &pid, &seqno, build_id) == 4) {
      uint64_t id = GET_ENTRY_ID(pid, seqno);
      auto entry_iter = map_data.find(id);
      if (entry_iter == map_data.end()) {
        FXL_LOG(ERROR) << "Missing entry (A record) for: " << line;
        continue;
      }
      (*entry_iter).second.build_id = build_id;
    } else if (sscanf(line,
                      "%[^@]@trace_load: %" PRIu64 ":%uc"
                      " %s %s",
                      prefix, &pid, &seqno, name, so_name) == 5) {
      uint64_t id = GET_ENTRY_ID(pid, seqno);
      auto entry_iter = map_data.find(id);
      if (entry_iter == map_data.end()) {
        FXL_LOG(ERROR) << "Missing entry (A record) for: " << line;
        continue;
      }
      LoadMap& entry = (*entry_iter).second;
      entry.name = name;
      entry.so_name = so_name;
      // We should now have the full record.
      AddLoadMap(entry);
    } else {
      FXL_VLOG(2) << fxl::StringPrintf("%d: ignoring", lineno);
    }
  }

  if (!feof(f)) {
    FXL_LOG(ERROR) << "Error reading file";
    return false;
  }

  return true;
}

void LoadMapTable::AddLoadMap(const LoadMap& map) {
  FXL_VLOG(2) << fxl::StringPrintf(
      "Adding map entry, pid %" PRIu64 " %s 0x%" PRIx64 "-0x%" PRIx64, map.pid,
      map.name.c_str(), map.load_addr, map.end_addr);
  maps_.push_back(map);
}

void LoadMapTable::Clear() { maps_.clear(); }

const LoadMap* LoadMapTable::LookupLoadMap(zx_koid_t pid, uint64_t addr) {
  for (auto& m : maps_) {
    if (pid == m.pid && addr >= m.load_addr && addr < m.end_addr)
      return &m;
  }

  return nullptr;
}

}  // namespace debugger_utils
