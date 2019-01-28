// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debug_directory.h"

#include <fs/pseudo-file.h>
#include <lib/fxl/strings/string_printf.h>
#include <algorithm>
#include "debug_info_retriever.h"

using fxl::StringPrintf;

namespace component {

DebugDirectory::ProcessThreads::ProcessThreads(const zx::process* process)
    : ExposedObject("threads"), process_(process) {
  auto all_dir = component::ObjectDir::Make("all_thread_stacks");
  all_dir.set_prop("stacks", [this]() -> std::string {
    return StringPrintf("\n%s", DebugInfoRetriever::GetInfo(process_).data());
  });

  object_dir().set_child(all_dir.object());
  object_dir().set_children_callback(
      [this](component::Object::ObjectVector* out_children) {
        fbl::Vector<ThreadInfo> threads;
        threads.reserve(kMaxThreads);
        GetThreads(&threads);

        for (const auto& thread : threads) {
          auto koid_string = StringPrintf("%lu", thread.koid);
          auto thread_obj = component::ObjectDir::Make(koid_string);
          thread_obj.set_prop("koid", koid_string);
          thread_obj.set_prop("name", thread.name.data());
          out_children->push_back(thread_obj.object());

          auto koid = thread.koid;
          auto stack_obj = component::ObjectDir::Make("stack");
          stack_obj.set_prop("dump", [this, koid]() -> std::string {
            zx_koid_t koids[] = {koid};
            return StringPrintf(
                "\n%s", DebugInfoRetriever::GetInfo(process_, koids, 1).data());
          });
          thread_obj.set_child(stack_obj.object());
        }
      });
}

void DebugDirectory::ProcessThreads::GetThreads(fbl::Vector<ThreadInfo>* out) {
  zx_koid_t thread_ids[kMaxThreads];
  size_t num_ids;
  if (process_->get_info(ZX_INFO_PROCESS_THREADS, thread_ids,
                         sizeof(zx_koid_t) * kMaxThreads, &num_ids,
                         nullptr) != ZX_OK) {
    return;
  }

  for (size_t i = 0; i < num_ids; i++) {
    zx::thread t;
    char name[ZX_MAX_NAME_LEN];
    if (process_->get_child(thread_ids[i], ZX_RIGHT_SAME_RIGHTS, &t) != ZX_OK) {
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
