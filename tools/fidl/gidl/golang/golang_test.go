// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package golang

import (
	"testing"
)

func TestIdentifierName(t *testing.T) {
	cases := []struct {
		fidlIdentifier, goIdentifier string
	}{
		{"a/Foo", "a.Foo"},
		{"A/Foo", "a.Foo"},
		{"a/foo_bar", "a.FooBar"},
		{"A/foo_bar", "a.FooBar"},
	}
	for _, ex := range cases {
		actual := identifierName(ex.fidlIdentifier)
		if actual != ex.goIdentifier {
			t.Errorf("with %s: expected %s, got %s", ex.fidlIdentifier, ex.goIdentifier, actual)
		}
	}
}
