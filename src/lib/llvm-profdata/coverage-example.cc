// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "coverage-example.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// This file defines functions that will be instrumented in a test run.
//
// NOTE: The end-to-end tests match expected line numbers in this file, so they
// will have to be changed if this file is touched at all.

namespace {

// This just avoids dead-code elimination and ICF without doing anything.
void Unique(const char* msg) { __asm__ volatile("" : : "r"(msg)); }

// This just avoids constant-folding so there is a real runtime test executed.
bool RunTimeBool(bool flag) {
  uint32_t x = flag;
  __asm__ volatile("" : "=r"(x) : "r"(x));
  return x;
}

}  // namespace

void LinkTimeDeadFunction() {
  // Statically uncovered line #30:
  Unique("LinkTimeDeadFunction");
}

void RunTimeDeadFunction() {
  // Dynamically uncovered line #35:
  Unique("RunTimeDeadFunction");
}

void MaybeCallRunTimeDeadFunction(bool doit) {
  // Dynamically covered line #40:
  if (doit) {
    // Dynamically uncovered line #42:
    RunTimeDeadFunction();
  }
}

void RunTimeCoveredFunction() {
  if (RunTimeBool(true)) {
    // Dynamically covered line #49:
    Unique("RunTimeCoveredFunction covered");
  } else {
    // Dynamically uncovered line #52:
    Unique("RunTimeCoveredFunction uncovered");
  }
}
