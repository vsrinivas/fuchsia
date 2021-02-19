// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rules

import (
	"regexp"
	"strings"
)

var spaceRe = regexp.MustCompile(`\s+`)

// normalizeLinkLabel normalizes link labels.
//
// "One label matches another just in case their normalized forms are equal. To
// normalize a label, strip off the opening and closing brackets, perform the
// Unicode case fold, strip leading and trailing whitespace and collapse
// consecutive internal whitespace to a single space."
//
// See https://spec.commonmark.org/0.29/
func normalizeLinkLabel(linkLabel string) string {
	normalized := linkLabel
	normalized = strings.ReplaceAll(normalized, "\n", " ")
	normalized = strings.TrimSpace(normalized)
	normalized = spaceRe.ReplaceAllString(normalized, " ")
	normalized = strings.ToLower(normalized)
	return normalized
}
