// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rustgen

import (
	"strings"
	"unicode"
	"unicode/utf8"
)

// Break a name into parts, discarding underscores but not changing case
func nameParts(name string) []string {
	var parts []string
	for _, namePart := range strings.Split(name, "_") {
		partStart := 0
		lastRune, _ := utf8.DecodeRuneInString(namePart)
		lastRuneStart := 0
		for i, curRune := range namePart {
			if i == 0 {
				continue
			}

			if unicode.IsUpper(curRune) && !unicode.IsUpper(lastRune) {
				parts = append(parts, namePart[partStart:i])
				partStart = i
			}
			if !(unicode.IsUpper(curRune) || unicode.IsDigit(curRune)) && unicode.IsUpper(lastRune) && partStart != lastRuneStart {
				parts = append(parts, namePart[partStart:lastRuneStart])
				partStart = lastRuneStart
			}

			lastRuneStart = i
			lastRune = curRune
		}
		parts = append(parts, namePart[partStart:])
	}
	return parts
}

// Convert a method name from fidl (CamelCase) to Rust (snake_case) style
func formatMethodName(name string) string {
	parts := nameParts(name)
	for i := range parts {
		parts[i] = strings.ToLower(parts[i])
	}

	return strings.Join(parts, "_")
}

// Convert an enum value name from fidl (ALL_CAPS_SNAKE) to Rust (CamelCase) style
// Note: this also works for union tags.
func formatEnumValue(name string) string {
	parts := nameParts(name)
	for i := range parts {
		parts[i] = strings.Title(strings.ToLower(parts[i]))
		if parts[i] == "" {
			parts[i] = "_"
		}
	}

	return strings.Join(parts, "")
}

// Convert a const name from fidl (kCamelCase) to Rust (ALL_CAPS_SNAKE) style
func formatConstName(name string) string {
	if len(name) >= 2 && name[0] == 'k' {
		secondRune, _ := utf8.DecodeRuneInString(name[1:])
		if unicode.IsUpper(secondRune) {
			name = name[1:]
		}
	}
	parts := nameParts(name)

	for i := range parts {
		parts[i] = strings.ToUpper(parts[i])
	}

	return strings.Join(parts, "_")
}

// Add an additional trailing underscore to any reserved word
func mangleReservedKeyword(name string) string {
	_, found := reservedRustKeywords[strings.TrimRight(name, "_")]
	if found {
		return name + "_"
	} else {
		return name
	}
}

// Identity formatter (used as argument to fidlToRustName)
func ident(name string) string {
	return name
}