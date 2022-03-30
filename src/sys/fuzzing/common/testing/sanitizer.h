// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_TESTING_SANITIZER_H_
#define SRC_SYS_FUZZING_COMMON_TESTING_SANITIZER_H_

namespace fuzzing {

// Simulate coverage being produced for a certain |index| in the PC table.
void SetCoverage(size_t index, uint8_t value);

// Simulate a memory allocation and record it with the fake sanitizer's malloc hook.
void Malloc(size_t size);

// Simulate dropping all references to a memory allocation, to be detected by the fake sanitizer.
void LeakMemory();

// Triggers a monitored condition for the fake sanitizer.
void Die();

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_TESTING_SANITIZER_H_
