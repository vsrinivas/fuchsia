// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dart-pkg/zircon/sdk_ext/natives.h"

#include <magenta/syscalls.h>

#include <stdio.h>
#include <string.h>

#include <memory>
#include <vector>

#include "dart/runtime/include/dart_api.h"
#include "dart-pkg/zircon/sdk_ext/handle.h"
#include "dart-pkg/zircon/sdk_ext/handle_waiter.h"
#include "dart-pkg/zircon/sdk_ext/system.h"
#include "lib/fxl/arraysize.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/tonic/dart_binding_macros.h"
#include "lib/tonic/dart_class_library.h"
#include "lib/tonic/dart_class_provider.h"
#include "lib/tonic/dart_library_natives.h"
#include "lib/tonic/dart_state.h"
#include "lib/tonic/logging/dart_invoke.h"
#include "lib/tonic/typed_data/uint8_list.h"

using tonic::ToDart;

namespace zircon {
namespace dart {
namespace {

static tonic::DartLibraryNatives* g_natives;

tonic::DartLibraryNatives* InitNatives() {
  tonic::DartLibraryNatives* natives = new tonic::DartLibraryNatives();
  HandleWaiter::RegisterNatives(natives);
  Handle::RegisterNatives(natives);
  System::RegisterNatives(natives);

  return natives;
}

Dart_NativeFunction NativeLookup(Dart_Handle name,
                                 int argument_count,
                                 bool* auto_setup_scope) {
  const char* function_name = nullptr;
  Dart_Handle result = Dart_StringToCString(name, &function_name);
  DART_CHECK_VALID(result);
  assert(function_name != nullptr);
  assert(auto_setup_scope != nullptr);
  *auto_setup_scope = true;
  if (!g_natives)
    g_natives = InitNatives();
  return g_natives->GetNativeFunction(name, argument_count, auto_setup_scope);
}

const uint8_t* NativeSymbol(Dart_NativeFunction native_function) {
  if (!g_natives)
    g_natives = InitNatives();
  return g_natives->GetSymbol(native_function);
}

}  // namespace

void Initialize() {
  Dart_Handle library = Dart_LookupLibrary(ToDart("dart:zircon"));
  DART_CHECK_VALID(library);
  DART_CHECK_VALID(Dart_SetNativeResolver(
      library, zircon::dart::NativeLookup, zircon::dart::NativeSymbol));

  auto dart_state = tonic::DartState::Current();
  std::unique_ptr<tonic::DartClassProvider> zircon_class_provider(
      new tonic::DartClassProvider(dart_state, "dart:zircon"));
  dart_state->class_library().add_provider("zircon",
                                           std::move(zircon_class_provider));
}

}  // namespace dart
}  // namespace zircon
