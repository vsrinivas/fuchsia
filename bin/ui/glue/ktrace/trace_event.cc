// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/glue/ktrace/trace_event.h"

#include <map>
#include <mutex>
#include <utility>

#include "apps/mozart/glue/ktrace/ktrace.h"
#include "lib/ftl/strings/string_printf.h"

namespace ktrace {
namespace {

class ProbeTable {
 public:
  ProbeTable() {}
  ~ProbeTable() {}

  static ProbeTable* GetInstance() {
    std::call_once(instance_initialized_, [] { instance_ = new ProbeTable(); });

    return instance_;
  }

  uint32_t GetProbeId(const char* cat, const char* name) {
    std::lock_guard<std::mutex> l(table_mutex_);

    auto it = table_.find(std::make_pair(cat, name));
    if (it != table_.cend())
      return it->second;

    uint32_t probe_id =
        TraceAddProbe(ftl::StringPrintf("%s/%s", cat, name).c_str());
    table_.emplace_hint(it,
                        std::make_pair(std::make_pair(cat, name), probe_id));
    return probe_id;
  }

 private:
  static std::once_flag instance_initialized_;
  static ProbeTable* instance_;

  std::mutex table_mutex_;
  std::map<std::pair<const char*, const char*>, uint32_t> table_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ProbeTable);
};

std::once_flag ProbeTable::instance_initialized_;
ProbeTable* ProbeTable::instance_;

void TraceWriteEvent(const char* cat,
                     const char* name,
                     uint32_t arg1,
                     uint32_t arg2) {
  TraceWriteProbe(ProbeTable::GetInstance()->GetProbeId(cat, name), arg1, arg2);
}

}  // namespace

void TraceEventDurationBegin(const char* cat, const char* name) {
  TraceWriteEvent(cat, name, 0, 1);
}

void TraceEventDurationEnd(const char* cat, const char* name) {
  TraceWriteEvent(cat, name, 0, 2);
}

void TraceEventAsyncBegin(const char* cat, const char* name, int id) {
  TraceWriteEvent(cat, name, id, 1);
}

void TraceEventAsyncEnd(const char* cat, const char* name, int id) {
  TraceWriteEvent(cat, name, id, 2);
}

void TraceEventInstant(const char* cat, const char* name) {
  TraceWriteEvent(cat, name, 0, 0);
}

}  // namespace ktrace
