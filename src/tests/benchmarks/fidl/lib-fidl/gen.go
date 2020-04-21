package main

import (
	"encoding/hex"
	"fmt"
	"math/rand"
	"strings"
)

// genString generates a random UTF8 string with a byte length equal or just
// over targetLen.
func genString(targetLen int) string {
	var r rune
	var b strings.Builder
	for b.Len() < targetLen {
		r = rune(rand.Int() % 0x1fffff)
		b.WriteRune(r)
	}
	return b.String()
}

const hexPerLine int = 32

func print(src string) {
	dst := make([]byte, hex.EncodedLen(len(src)))
	hex.Encode(dst, []byte(src))

	fmt.Printf("const char S_%d[] = \n", len(src))
	for i := 0; i < len(dst); i += 2 {
		if i%hexPerLine == 0 {
			fmt.Printf("\"")
		}
		fmt.Printf("\\x%s", dst[i:i+2])
		if i != len(dst)-2 && i%hexPerLine == hexPerLine-2 {
			fmt.Printf("\"\n")
		}
	}
	fmt.Printf("\";\n")
}

func main() {
	fmt.Print(top)
	for _, size := range []int{
		256,
		1024,
		4096,
		16384,
		65536,
	} {
		s := genString(size)
		print(s)
	}
	fmt.Print(bottom)
}

const top = `// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// File is automatically generated; do not edit. To update, use:
//
// go run src/tests/benchmarks/fidl/lib-fidl/gen.go > src/tests/benchmarks/fidl/lib-fidl/data.h
// fx format-code --files=src/tests/benchmarks/fidl/lib-fidl/data.h

#ifndef SRC_TESTS_BENCHMARKS_FIDL_LIB_FIDL_DATA_H_
#define SRC_TESTS_BENCHMARKS_FIDL_LIB_FIDL_DATA_H_

namespace lib_fidl_benchmarks {

`

const bottom = `

}  // namespace lib_fidl_benchmarks

#endif  // SRC_TESTS_BENCHMARKS_FIDL_LIB_FIDL_DATA_H_
`
