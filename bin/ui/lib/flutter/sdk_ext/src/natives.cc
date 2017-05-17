// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/flutter/sdk_ext/src/natives.h"

#include <stdio.h>
#include <string.h>

#include <memory>
#include <vector>

#include "apps/mozart/services/views/view_manager.fidl.h"
#include "lib/ftl/arraysize.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"

namespace mozart {

#define REGISTER_FUNCTION(name, count) {"" #name, name, count},
#define DECLARE_FUNCTION(name, count) \
  extern void name(Dart_NativeArguments args);

#define MOZART_NATIVE_LIST(V)         \
  V(Mozart_offerServiceProvider, 2)

MOZART_NATIVE_LIST(DECLARE_FUNCTION);

static struct NativeEntries {
  const char* name;
  Dart_NativeFunction function;
  int argument_count;
} Entries[] = {MOZART_NATIVE_LIST(REGISTER_FUNCTION)};

Dart_NativeFunction NativeLookup(Dart_Handle name,
                                 int argument_count,
                                 bool* auto_setup_scope) {
  const char* function_name = nullptr;
  Dart_Handle result = Dart_StringToCString(name, &function_name);
  DART_CHECK_VALID(result);
  assert(function_name != nullptr);
  assert(auto_setup_scope != nullptr);
  *auto_setup_scope = true;
  size_t num_entries = arraysize(Entries);
  for (size_t i = 0; i < num_entries; ++i) {
    const struct NativeEntries& entry = Entries[i];
    if (!strcmp(function_name, entry.name) &&
        (entry.argument_count == argument_count)) {
      return entry.function;
    }
  }
  return nullptr;
}

const uint8_t* NativeSymbol(Dart_NativeFunction native_function) {
  size_t num_entries = arraysize(Entries);
  for (size_t i = 0; i < num_entries; ++i) {
    const struct NativeEntries& entry = Entries[i];
    if (entry.function == native_function) {
      return reinterpret_cast<const uint8_t*>(entry.name);
    }
  }
  return nullptr;
}

#define CHECK_INTEGER_ARGUMENT(args, num, result)                \
  {                                                              \
    Dart_Handle __status;                                        \
    __status = Dart_GetNativeIntegerArgument(args, num, result); \
    if (Dart_IsError(__status)) {                                \
      return;                                                    \
    }                                                            \
  }

NativesDelegate::~NativesDelegate() {}

void Mozart_offerServiceProvider(Dart_NativeArguments args) {
  intptr_t context = 0;
  int64_t handle = 0;
  CHECK_INTEGER_ARGUMENT(args, 0, &context);
  CHECK_INTEGER_ARGUMENT(args, 1, &handle);

  if (!context || !handle)
    return;

  NativesDelegate* delegate = reinterpret_cast<NativesDelegate*>(context);
  fidl::InterfaceHandle<app::ServiceProvider> provider =
      fidl::InterfaceHandle<app::ServiceProvider>
          (mx::channel(static_cast<mx_handle_t>(handle)), 0);

  View* view = delegate->GetMozartView();
  view->OfferServiceProvider(std::move(provider));
}

}  // namespace mozart
