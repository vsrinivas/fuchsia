// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_UTEST_SANITIZER_SANITIZER_MEMORY_SNAPSHOT_TEST_DSO_H_
#define ZIRCON_SYSTEM_UTEST_SANITIZER_SANITIZER_MEMORY_SNAPSHOT_TEST_DSO_H_

extern "C" {

void* NeededDsoDataPointer();
void* NeededDsoBssPointer();
const void* NeededDsoRodataPointer();
const void* NeededDsoRelroPointer();
void* NeededDsoThreadLocalDataPointer();
void* NeededDsoThreadLocalBssPointer();

void* DlopenDsoDataPointer();
void* DlopenDsoBssPointer();
const void* DlopenDsoRodataPointer();
const void* DlopenDsoRelroPointer();
void* DlopenDsoThreadLocalDataPointer();
void* DlopenDsoThreadLocalBssPointer();

}  // extern "C"

#endif  // ZIRCON_SYSTEM_UTEST_SANITIZER_SANITIZER_MEMORY_SNAPSHOT_TEST_DSO_H_
