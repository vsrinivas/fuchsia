// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package util

import (
	"fmt"
	"gen/config"
	"strings"
)

// Generates a string containing a sequence of the specified size.
// 0x01, 0x02, ...
func List(n int, values ValueGenerator) string {
	if n <= 256 {
		// Note: if this if block wasn't here, then a comment would be
		// generated when n == 256.
		return ListNoComments(n, values)
	}

	var builder strings.Builder
	bytes256 := ListNoComments(256, values)
	nBlocks256 := n / 256
	nRemaining := n % 256

	for i := 0; i < nBlocks256; i++ {
		start := i*256 + 1
		end := (i + 1) * 256
		if i > 0 {
			builder.WriteString("\n\n")
		}
		builder.WriteString(fmt.Sprintf("// %d - %d\n", start, end))
		builder.WriteString(bytes256)
	}

	if nRemaining > 0 {
		builder.WriteString("\n\n")
		start := nBlocks256*256 + 1
		end := nBlocks256*256 + nRemaining
		builder.WriteString(fmt.Sprintf("// %d - %d\n", start, end))
		builder.WriteString(ListNoComments(nRemaining, values))
	}
	return builder.String()
}

// Generates a string containing a sequence of a specified size that has
// no section breaks or comments.
func ListNoComments(n int, values ValueGenerator) string {
	var builder strings.Builder
	for i := 0; i < n/8; i++ {
		if i > 0 {
			builder.WriteRune('\n')
		}
		builder.WriteString(fmt.Sprintf("%s, %s, %s, %s, %s, %s, %s, %s,",
			values.next(), values.next(), values.next(), values.next(), values.next(), values.next(), values.next(), values.next()))
	}
	if n/8 > 0 && n%8 > 0 {
		builder.WriteRune('\n')
	}
	for i := 0; i < n%8; i++ {
		if i != 0 {
			builder.WriteRune(' ')
		}
		builder.WriteString(fmt.Sprintf("%s,", values.next()))
	}
	return builder.String()
}

func Fields(start, end int, fieldPrefix string, values ValueGenerator) string {
	var builder strings.Builder
	for i := start; i <= end; i++ {
		if i > start {
			builder.WriteRune('\n')
		}
		builder.WriteString(fmt.Sprintf("%s%d: %s,", fieldPrefix, i, values.next()))
	}
	return builder.String()
}

func RepeatHandleDef(hd config.HandleDef, n int) []config.HandleDef {
	defs := make([]config.HandleDef, n)
	for i := range defs {
		defs[i] = hd
	}
	return defs
}
