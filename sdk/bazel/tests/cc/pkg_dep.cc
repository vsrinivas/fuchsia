// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/namespace.h>

int main(int argc, const char** argv) {
  fdio_ns_create(NULL);
}
