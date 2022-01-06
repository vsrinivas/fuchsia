// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package librust

import (
	"bytes"
	"encoding/hex"
	"fmt"
	"strings"
)

// BuildBytes generates an array literal representing the bytes provided, encoded as hex.
func BuildBytes(bytes []byte) string {
	var builder strings.Builder
	builder.WriteString("[\n")
	for i, b := range bytes {
		builder.WriteString(fmt.Sprintf("0x%02x,", b))
		if i%8 == 7 {
			builder.WriteString("\n")
		}
	}
	builder.WriteString("]")
	return builder.String()
}

// EscapeStr generates a string literal representing the string provided, properly escaped for
// a source file.
func EscapeStr(value string) string {
	var (
		buf    bytes.Buffer
		src    = []byte(value)
		dstLen = hex.EncodedLen(len(src))
		dst    = make([]byte, dstLen)
	)
	hex.Encode(dst, src)
	for i := 0; i < dstLen; i += 2 {
		buf.WriteString("\\x")
		buf.WriteByte(dst[i])
		buf.WriteByte(dst[i+1])
	}
	return buf.String()
}
