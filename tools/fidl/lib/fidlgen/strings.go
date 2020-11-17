// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen

import (
	"fmt"
	"strings"
)

func SingleQuote(s string) string {
	s = strings.ReplaceAll(s, `\`, `\\`)
	s = strings.ReplaceAll(s, `'`, `\'`)
	return fmt.Sprintf(`'%s'`, s)
}

// PrintableASCIIRune reports whether r is a printable ASCII rune, i.e. in the
// range 0x20 to 0x7E.
func PrintableASCIIRune(r rune) bool {
	return 0x20 <= r && r <= 0x7e
}

// PrintableASCII reports whether s is made of only printable ASCII runes.
func PrintableASCII(s string) bool {
	for _, r := range s {
		if !PrintableASCIIRune(r) {
			return false
		}
	}
	return true
}
