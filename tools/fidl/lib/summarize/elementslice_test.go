// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package summarize

import (
	"fmt"
	"testing"
)

func TestFQNLess(t *testing.T) {
	tests := []struct {
		e1, e2 Name
		less   bool
	}{
		{e1: "l", e2: "l/foo", less: false},
		{e1: "l/foo", e2: "l", less: true},
		{e1: "l/foo", e2: "l/foo.foo", less: false},
		{e1: "l/foo.foo", e2: "l/foo", less: true},
		{e1: "l/bar", e2: "l/foo", less: true},
		{e1: "l/bar.foo", e2: "l/foo", less: true},
		{e1: "l/bar.foo", e2: "l/foo.baz", less: true},
		{e1: "l/bar", e2: "l/foo.foo", less: true},
		{e1: "l/bar", e2: "l/foo.foo", less: true},
		{e1: "l/bar.bar", e2: "l/foo", less: true},
		{e1: "p.l", e2: "p.l/foo", less: false},
		{e1: "p.l/foo", e2: "p.l", less: true},
	}
	for i, test := range tests {
		t.Run(fmt.Sprintf("t-%d", i), func(t *testing.T) {
			actual := newFqn(test.e1).Less(newFqn(test.e2))
			if test.less != actual {
				t.Errorf("got: %v, want: %v for %+v", actual, test.less, test)
			}
		})
	}
}
