// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <string.h>

int strcmp(char const *cs, char const *ct) {
  int res;

  while (1) {
    if ((res = *cs - *ct++) != 0 || !*cs++)
      break;
  }

  return res;
}
