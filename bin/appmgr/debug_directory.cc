// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debug_directory.h"
#include <fbl/string_printf.h>
#include <fs/pseudo-file.h>
#include <lib/fxl/strings/split_string.h>
#include <algorithm>
#include "debug_info_retriever.h"

using fbl::StringPrintf;

namespace component {

fbl::RefPtr<fs::Vnode> MakeThreadDumpFile(
    std::function<fbl::String(void)> content_callback) {
  return fbl::AdoptRef(new fs::BufferedPseudoFile(
      [content_callback = std::move(content_callback)](fbl::String* out) {
        *out = content_callback();
        return ZX_OK;
      }));
}

zx_status_t DebugDirectory::GetFile(fbl::RefPtr<Vnode>* out, uint64_t id,
                                    fbl::String name) {
  if (id == kAllId) {
    *out = fbl::AdoptRef(new fs::BufferedPseudoFile([this](fbl::String* out) {
      *out = DebugInfoRetriever::GetInfo(&process_);
      return ZX_OK;
    }));
    return ZX_OK;
  }

  *out = fbl::AdoptRef(new fs::BufferedPseudoFile([this, id](fbl::String* out) {
    zx_koid_t ids[] = {id};
    *out = DebugInfoRetriever::GetInfo(&process_, ids, 1);
    return ZX_OK;
  }));

  return ZX_OK;
}  // namespace component

void DebugDirectory::GetContents(LazyEntryVector* out_vector) {
  out_vector->push_back({kAllId, "all", V_TYPE_DIR});

  fbl::Vector<ThreadInfo> info;
  info.reserve(kMaxThreads);
  GetThreads(&info);

  for (const auto& thread : info) {
    out_vector->push_back({thread.koid, thread.name, V_TYPE_FILE});
  }
}

void DebugDirectory::GetThreads(fbl::Vector<ThreadInfo>* out) {
  zx_koid_t thread_ids[kMaxThreads];
  size_t num_ids;
  if (process_.get_info(ZX_INFO_PROCESS_THREADS, thread_ids,
                        sizeof(zx_koid_t) * kMaxThreads, &num_ids,
                        nullptr) != ZX_OK) {
    return;
  }

  for (size_t i = 0; i < num_ids; i++) {
    zx::thread t;
    char name[ZX_MAX_NAME_LEN];
    if (process_.get_child(thread_ids[i], ZX_RIGHT_SAME_RIGHTS, &t) != ZX_OK) {
      return;
    }
    if (t.get_property(ZX_PROP_NAME, &name, ZX_MAX_NAME_LEN) != ZX_OK) {
      return;
    }
    t.get_property(ZX_PROP_NAME, &name, ZX_MAX_NAME_LEN);
    out->push_back({thread_ids[i], name, std::move(t)});
  }
}

}  // namespace component
