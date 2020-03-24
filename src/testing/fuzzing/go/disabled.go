// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !libfuzzer

package fuzzing

// Enabled reports whether fuzzing instrumentation is enabled for this
// build. It can be used to conditionally disable fuzzer-hostile
// assertions.
const Enabled = false
