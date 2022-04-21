// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <string.h>

int strncmp(char const *cs, char const *ct, size_t count) {
  int res = 0;

  while (count > 0) {
    if ((res = *cs - *ct++) != 0 || !*cs++)
      break;
    count--;
  }

  return res;
}
