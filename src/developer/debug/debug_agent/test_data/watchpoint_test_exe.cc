// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

int SomeInt = 10;
int SomeInt2 = 20;
int SomeInt3 = 30;
int SomeInt4 = 40;

int main() {
  printf("Before: %d, %d, %d, %d\n", SomeInt, SomeInt2, SomeInt3, SomeInt4);
  SomeInt++;
  SomeInt2++;
  SomeInt3++;
  SomeInt4++;
  printf("After:  %d, %d, %d, %d\n", SomeInt, SomeInt2, SomeInt3, SomeInt4);
}
