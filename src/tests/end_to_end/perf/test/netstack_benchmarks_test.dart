// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'helpers.dart';

void main() {
  enableLoggingOutput();

  addTspecTest(
      '/pkgfs/packages/netstack_benchmarks/0/data/udp_benchmark.tspec');
}
