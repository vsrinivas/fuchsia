// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"runtime"
	"testing"

	"github.com/google/go-cmp/cmp"
)

func assertEqual(t *testing.T, left interface{}, right interface{}, opts ...cmp.Option) {
	if !cmp.Equal(left, right, opts...) {
		_, file, line, ok := runtime.Caller(1)
		if !ok {
			panic("Failed to get caller.")
		}
		t.Fatalf(
			`
At %s:%v
Required left/right to be equal, but
left:
%+v

right:
%+v

diff:
%v
`,
			file, line, left, right, cmp.Diff(left, right, opts...))
	}
}

func expectEqual(t *testing.T, left interface{}, right interface{}, opts ...cmp.Option) {
	if !cmp.Equal(left, right, opts...) {
		_, file, line, ok := runtime.Caller(1)
		if !ok {
			panic("Failed to get caller.")
		}
		t.Errorf(
			`
At %s:%v
Expected left/right to be equal, but
left:
%+v

right:
%+v

diff:
%v
`,
			file, line, left, right, cmp.Diff(left, right, opts...))
	}
}

func assertNotEqual(t *testing.T, left interface{}, right interface{}, opts ...cmp.Option) {
	if cmp.Equal(left, right, opts...) {
		_, file, line, ok := runtime.Caller(1)
		if !ok {
			panic("Failed to get caller.")
		}
		t.Fatalf(`
At %s:%v
Required left/right to be different, but
left=
%+v
=right
`, file, line, left)
	}
}

func expectNotEqual(t *testing.T, left interface{}, right interface{}, opts ...cmp.Option) {
	if cmp.Equal(left, right, opts...) {
		_, file, line, ok := runtime.Caller(1)
		if !ok {
			panic("Failed to get caller.")
		}
		t.Errorf(`
At %s:%v
Expected left/right to be different, but
left=
%+v
=right
`, file, line, left)
	}
}

func assertPanic(t *testing.T) {
	if r := recover(); r == nil {
		t.Errorf("The code did not panic")
	}
}
