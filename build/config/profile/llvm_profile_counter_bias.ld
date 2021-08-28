/* Copyright 2019 The Fuchsia Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* TODO(fxbug.dev/83653): This symbol is normally provided by the compiler
 * only when profile runtime is needed, but due to a compiler bug, we're
 * seeing undefined reference to this symbol even in cases when the runtime
 * isn't needed hence this workaround.
 */
PROVIDE_HIDDEN(__llvm_profile_counter_bias = 0);
