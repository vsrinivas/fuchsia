// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

// #include <stddef.h>
// #include <stdint.h>
import "C"

import (
	"unsafe"

	target "GO_FUZZER_PKG" // replaced by go_fuzzer.gni
)

//export LLVMFuzzerTestOneInput
func LLVMFuzzerTestOneInput(data *C.uint8_t, size C.size_t) C.int {
	s := make([]byte, size)
	if size != 0 {
		copy(s, (*[1 << 30]byte)(unsafe.Pointer(data))[:size:size])
	}

	target.GO_FUZZER_FUNC(s)
	return 0
}

func main() {
}
