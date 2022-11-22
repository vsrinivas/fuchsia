# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
from build.bazel.tests.hello_python import lib

print('Fib(5)=%s' % lib.Fib(5))