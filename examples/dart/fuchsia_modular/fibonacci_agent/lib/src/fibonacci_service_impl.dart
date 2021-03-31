// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_fibonacci/fidl_async.dart' as fidl_fib;

class FibonacciServiceImpl extends fidl_fib.FibonacciService {
  @override
  Future<int> calcFibonacci(int n) async {
    return _fib(n);
  }

  int _fib(int n) {
    if (n <= 1) {
      return n;
    }
    return _fib(n - 1) + _fib(n - 2);
  }
}
