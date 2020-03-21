// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package flagmisc

import (
	"flag"
	"testing"
)

func assertEqual(t *testing.T, a, b string) {
	if a != b {
		t.Errorf("expected the following to be the same:\n%q\n%q\n", a, b)
	}
}

func setAndCompare(t *testing.T, f flag.Value, val, expected string) {
	if err := f.Set(val); err != nil {
		t.Fatalf("could not set flag value %q on %v", val, f)
	}
	assertEqual(t, expected, f.String())
}

func TestFlagImplementations(t *testing.T) {
	// Sanity check that these types do indeed implement flag.Value. This would
	// otherwise give a compile-time error.
	var _ flag.Value = (*StringsValue)(nil)
}

func TestStringsValue(t *testing.T) {
	rs := &StringsValue{}
	assertEqual(t, rs.String(), "")
	setAndCompare(t, rs, "a", "a")
	setAndCompare(t, rs, "b", "a, b")
	setAndCompare(t, rs, "c", "a, b, c")
}
