// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/compiler.h>

#include "sanitizer-memory-snapshot-test-dso.h"

namespace {

int gDlopenDsoData = 17;

int gDlopenDsoBss;

thread_local int gDlopenDsoThreadLocalData = 17;

thread_local int gDlopenDsoThreadLocalBss;

const int gDlopenDsoRodata = 23;

int* const gDlopenDsoRelro = &gDlopenDsoData;

}  // namespace

__EXPORT void* DlopenDsoDataPointer() { return &gDlopenDsoData; }

__EXPORT void* DlopenDsoBssPointer() { return &gDlopenDsoBss; }

__EXPORT const void* DlopenDsoRodataPointer() { return &gDlopenDsoRodata; }

__EXPORT const void* DlopenDsoRelroPointer() { return &gDlopenDsoRelro; }

__EXPORT void* DlopenDsoThreadLocalDataPointer() { return &gDlopenDsoThreadLocalData; }

__EXPORT void* DlopenDsoThreadLocalBssPointer() { return &gDlopenDsoThreadLocalBss; }
