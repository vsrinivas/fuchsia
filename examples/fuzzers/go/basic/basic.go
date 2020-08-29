// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package basic

import (
	fuzzing "go.fuchsia.dev/fuchsia/src/testing/fuzzing/go"
)

func init() {
	if fuzzing.Enabled {
		println("Compiled with fuzzing instrumentation")
	} else {
		println("Compiled without fuzzing instrumentation")
	}
}

func Fuzz(s []byte) {
	if len(s) == 4 && s[0] == 'F' && s[1] == 'U' && s[2] == 'Z' && s[3] == 'Z' {
		panic("fuzzed")
	}
}
