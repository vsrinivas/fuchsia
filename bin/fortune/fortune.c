// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static const size_t kFortunesSize = 4;
static const char* kFortunes[4] = {
    "If we have data, let’s look at data. If all we have are opinions, let’s "
    "go\n"
    "with mine. -- Jim Barksdale",
    "Things that are impossible just take longer.",
    "Better lucky than good.",
    "Fortune favors the bold.",
};

int main(int argc, char** argv) {
  srand(time(0));
  printf("%s\n", kFortunes[rand() % kFortunesSize]);
  return 0;
}
