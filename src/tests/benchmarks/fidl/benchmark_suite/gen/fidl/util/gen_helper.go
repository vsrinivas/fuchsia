// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package util

import (
	"fmt"
	"strings"

	"go.fuchsia.dev/fuchsia/src/tests/benchmarks/fidl/benchmark_suite/gen/types"
)

// Define fields for structs.
func StructFields(typ types.FidlType, namePrefix string, n int) string {
	var builder strings.Builder
	for i := 1; i <= n; i++ {
		if i > 1 {
			builder.WriteRune('\n')
		}
		builder.WriteString(fmt.Sprintf("%s%d %s;", namePrefix, i, typ))
	}
	return builder.String()
}

// Define fields for types with ordinals (union, table).
func OrdinalFields(typ types.FidlType, namePrefix string, n int) string {
	var builder strings.Builder
	for i := 1; i <= n; i++ {
		if i > 1 {
			builder.WriteRune('\n')
		}
		builder.WriteString(fmt.Sprintf("%d: %s%d %s;", i, namePrefix, i, typ))
	}
	return builder.String()
}

// Define a range of reserved fields from [start, end].
func ReservedFields(start, end int) string {
	var builder strings.Builder
	for i := start; i <= end; i++ {
		if i > start {
			builder.WriteRune(('\n'))
		}
		builder.WriteString(fmt.Sprintf("%d: reserved;", i))
	}
	return builder.String()
}
