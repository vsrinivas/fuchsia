// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/ktrace.h>
#include <lib/ktrace/string_ref.h>

#include <ktl/atomic.h>

int StringRef::Register(StringRef* string_ref) {
  // Return the id if the string ref is already registered.
  int id = string_ref->id.load(ktl::memory_order_relaxed);
  if (id != kInvalidId) {
    return id;
  }

  // Try to set the id of the string ref. When there is a race with other
  // threads or CPUs only one will succeed, in which case the id counter will
  // harmlessly skip values equal to the number of agents racing between here
  // and the check above.
  const int new_id = id_counter_.fetch_add(1, ktl::memory_order_relaxed);
  while (!string_ref->id.compare_exchange_weak(id, new_id, ktl::memory_order_relaxed,
                                               ktl::memory_order_relaxed)) {
    // If another agent set the id first simply return the id.
    if (id != kInvalidId && id != new_id)
      return id;
  }

  // Emit a name record the first time this string ref is encountered at
  // runtime. This is ignored if tracing is not active and is replayed at the
  // beginning of subsequent tracing sessions.
  ktrace_name_etc(TAG_PROBE_NAME, string_ref->id, 0, string_ref->string, true);

  // Register the string ref in the global linked list. When there is a race
  // above only the winning agent that set the id will continue to this point.
  string_ref->next = head_.load(ktl::memory_order_relaxed);
  while (!head_.compare_exchange_weak(string_ref->next, string_ref, ktl::memory_order_release,
                                      ktl::memory_order_relaxed)) {
  }

  return new_id;
}

ktl::atomic<int> StringRef::id_counter_{StringRef::kInvalidId + 1};
ktl::atomic<StringRef*> StringRef::head_{nullptr};
