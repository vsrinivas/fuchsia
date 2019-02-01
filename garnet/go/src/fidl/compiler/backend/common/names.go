// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package common

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

// Convert text to snake_case style
func ToSnakeCase(name string) string {
	parts := nameParts(name)
	for i := range parts {
		parts[i] = strings.ToLower(parts[i])
	}
	return strings.Join(parts, "_")
}

// Convert a name to UpperCamelCase style
func ToUpperCamelCase(name string) string {
	parts := nameParts(name)
	for i := range parts {
		parts[i] = strings.Title(strings.ToLower(parts[i]))
		if parts[i] == "" {
			parts[i] = "_"
		}
	}
	return strings.Join(parts, "")
}

// Convert a name to lowerCamelCase style
func ToLowerCamelCase(name string) string {
	parts := nameParts(name)
	for i := range parts {
		if i == 0 {
			parts[i] = strings.ToLower(parts[i])
		} else {
			parts[i] = strings.Title(strings.ToLower(parts[i]))
		}
		if parts[i] == "" {
			parts[i] = "_"
		}
	}
	return strings.Join(parts, "")
}

// Convert text to friendly case style (like snake case, but with spaces)
func ToFriendlyCase(name string) string {
	parts := nameParts(name)
	for i := range parts {
		parts[i] = strings.ToLower(parts[i])
	}
	return strings.Join(parts, " ")
}

// Convert a const name from kCamelCase to ALL_CAPS_SNAKE style
func ConstNameToAllCapsSnake(name string) string {
	parts := nameParts(RemoveLeadingK(name))
	for i := range parts {
		parts[i] = strings.ToUpper(parts[i])
	}
	return strings.Join(parts, "_")
}

// Removes a leading 'k' if the second character is upper-case, otherwise
// returns the argument
func RemoveLeadingK(name string) string {
	if len(name) >= 2 && name[0] == 'k' {
		secondRune, _ := utf8.DecodeRuneInString(name[1:])
		if unicode.IsUpper(secondRune) {
			name = name[1:]
		}
	}
	return name
}
