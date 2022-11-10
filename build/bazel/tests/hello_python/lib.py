# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


def Fib(n):
    if n == 0 or n == 1:
        return n
    return Fib(n - 1) + Fib(n - 2)
