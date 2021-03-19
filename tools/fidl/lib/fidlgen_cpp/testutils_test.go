// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"fmt"
	"testing"

	"github.com/google/go-cmp/cmp"
)

func performEqualCheck(left interface{}, right interface{}, opts ...cmp.Option) (bool, string) {
	var failed bool
	var log string
	if !cmp.Equal(left, right, opts...) {
		failed = true
		log = fmt.Sprintf(
			`
Expected left/right to be equal, but
left:
%+v

right:
%+v

diff:
%v
`,
			left, right, cmp.Diff(left, right, opts...))
	}
	return failed, log
}

func assertEqual(t *testing.T, left interface{}, right interface{}, opts ...cmp.Option) {
	t.Helper()
	if failed, log := performEqualCheck(left, right, opts...); failed {
		t.Fatal(log)
	}
}

func expectEqual(t *testing.T, left interface{}, right interface{}, opts ...cmp.Option) {
	t.Helper()
	if failed, log := performEqualCheck(left, right, opts...); failed {
		t.Error(log)
	}
}

func performNotEqualCheck(left interface{}, right interface{}, opts ...cmp.Option) (failed bool, log string) {
	if cmp.Equal(left, right, opts...) {
		failed = true
		log = fmt.Sprintf(`
Expected left/right to be different, but
left=
%+v
=right
`, left)
	}
	return
}

func assertNotEqual(t *testing.T, left interface{}, right interface{}, opts ...cmp.Option) {
	t.Helper()
	if failed, log := performNotEqualCheck(left, right, opts...); failed {
		t.Fatal(log)
	}
}

func expectNotEqual(t *testing.T, left interface{}, right interface{}, opts ...cmp.Option) {
	t.Helper()
	if failed, log := performNotEqualCheck(left, right, opts...); failed {
		t.Error(log)
	}
}

func assertPanicOccurs(t *testing.T) {
	if r := recover(); r == nil {
		t.Errorf("The code did not panic")
	}
}
