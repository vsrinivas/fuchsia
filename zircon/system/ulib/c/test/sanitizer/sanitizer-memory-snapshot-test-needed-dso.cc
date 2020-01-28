// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/compiler.h>

#include "sanitizer-memory-snapshot-test-dso.h"

namespace {

int gNeededDsoData = 17;

int gNeededDsoBss;

thread_local int gNeededDsoThreadLocalData = 17;

thread_local int gNeededDsoThreadLocalBss;

const int gNeededDsoRodata = 23;

int* const gNeededDsoRelro = &gNeededDsoData;

}  // namespace

__EXPORT void* NeededDsoDataPointer() { return &gNeededDsoData; }

__EXPORT void* NeededDsoBssPointer() { return &gNeededDsoBss; }

__EXPORT const void* NeededDsoRodataPointer() { return &gNeededDsoRodata; }

__EXPORT const void* NeededDsoRelroPointer() { return &gNeededDsoRelro; }

__EXPORT void* NeededDsoThreadLocalDataPointer() { return &gNeededDsoThreadLocalData; }

__EXPORT void* NeededDsoThreadLocalBssPointer() { return &gNeededDsoThreadLocalBss; }
