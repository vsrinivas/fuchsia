// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rules

import (
	"testing"
)

func TestCasingOfAnchors(t *testing.T) {
	ruleTestCase{
		files: map[string]string{
			"example.md": `# Hello

## World {#good-anchor}

## Monde «{#:bad-anchor}»

## Welt «{#bad_anchor}»`,
		},
	}.runOverTokens(t, newCasingOfAnchors)
}

func TestCasingOfAnchors_fuchsiaDevExt(t *testing.T) {
	ruleTestCase{
		files: map[string]string{
			"example.md": `
We have extensions in fuchsia.dev. They should not trigger errors:

# {{ rfc.name }} - {{ rfc.title }}`,
		},
	}.runOverTokens(t, newCasingOfAnchors)
}
