// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package main

import (
	"strings"

	"go.fuchsia.dev/fuchsia/src/tests/benchmarks/fidl/benchmark_suite/gen/config"
)

// Strip out existing indentation and use count of open braces to place new indentation.
func formatObj(initialIndent int, value string) string {
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

func formatComment(comment string) string {
	if comment == "" {
		return ""
	}
	parts := strings.Split(strings.TrimSpace(comment), "\n")
	var builder strings.Builder
	for _, part := range parts {
		builder.WriteString("// " + strings.TrimSpace(part) + "\n")
	}
	return builder.String()
}

func formatBindingList(bindings []config.Binding) string {
	if len(bindings) == 0 {
		return ""
	}
	strs := make([]string, len(bindings))
	for i, binding := range bindings {
		strs[i] = string(binding)
	}
	return "[" + strings.Join(strs, ", ") + "]"
}
