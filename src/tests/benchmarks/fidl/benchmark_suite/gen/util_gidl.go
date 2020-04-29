// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"strings"
)

// Generates a string containing a byte sequence of the specified size.
// 0x01, 0x02, ...
func gidlBytes(n int) string {
	if n <= 256 {
		return gidlBytesNoComments(n)
	}

	var builder strings.Builder
	bytes256 := gidlBytesNoComments(256)
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
		builder.WriteString(gidlBytesNoComments(nRemaining))
	}
	return builder.String()
}

// Generates a string containing a byte sequence of a specified size that has
// no section breaks or comments.
func gidlBytesNoComments(n int) string {
	var builder strings.Builder
	for i := 0; i < n/8; i++ {
		if i > 0 {
			builder.WriteRune('\n')
		}
		builder.WriteString(fmt.Sprintf("%#02x, %#02x, %#02x, %#02x, %#02x, %#02x, %#02x, %#02x,",
			i*8, i*8+1, i*8+2, i*8+3, i*8+4, i*8+5, i*8+6, i*8+7))
	}
	if n/8 > 0 && n%8 > 0 {
		builder.WriteRune('\n')
	}
	for i := 0; i < n%8; i++ {
		if i != 0 {
			builder.WriteRune(' ')
		}
		builder.WriteString(fmt.Sprintf("%#02x,", n/8*8+i))
	}
	return builder.String()
}
