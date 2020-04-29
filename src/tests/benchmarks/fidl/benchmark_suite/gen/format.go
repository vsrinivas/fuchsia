// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import "strings"

// Strip out existing indentation and use count of open braces to place new indentation.
func format(initialIndent int, value string) string {
	indentationMark := "    "
	nIndent := initialIndent
	var builder strings.Builder

	runes := []rune(strings.Trim(value, " \t\n"))
	for i := 0; i < len(runes); i++ {
		r := runes[i]
		builder.WriteRune(r)
		switch r {
		case '{', '[':
			nIndent++
		case '}', ']':
			nIndent--
		case '\n':
		loop:
			for i+1 < len(runes) {
				switch runes[i+1] {
				case ' ', '\t':
					i++
				default:
					break loop
				}
			}
			effectiveIndent := nIndent
			if i+1 < len(runes) && (runes[i+1] == '}' || runes[i+1] == ']') {
				effectiveIndent--
			}
			if i+1 == len(runes) || runes[i+1] != '\n' {
				builder.WriteString(strings.Repeat(indentationMark, effectiveIndent))
			}
		}
	}
	return builder.String()
}
