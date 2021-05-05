// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package time

import (
	"math"
	"syscall/zx"
	"testing"
)

func TestTimeAdd(t *testing.T) {
	for _, tc := range []struct {
		a    Time
		b    Duration
		want Time
	}{
		{Time{2}, Duration(2), Time{4}},
		{Time{2}, Duration(-1), Time{1}},
		{Time{1}, Duration(math.MaxInt64), Time{math.MaxInt64}},
		{Time{-1}, Duration(math.MinInt64), Time{math.MinInt64}},
	} {
		if got, want := tc.a.Add(tc.b), tc.want; got != want {
			t.Errorf("Time(%d).Add(%d) failed: got=%d, want=%d", tc.a, tc.b, got, want)
		}
	}
}

func TestTimeSub(t *testing.T) {
	for _, tc := range []struct {
		a    Time
		b    Time
		want Duration
	}{
		{Time{2}, Time{1}, Duration(1)},
		{Time{2}, Time{-2}, Duration(4)},
		{Time{1}, Time{math.MinInt64}, Duration(math.MaxInt64)},
		{Time{-1}, Time{math.MaxInt64}, Duration(math.MinInt64)},
	} {
		if got, want := tc.a.Sub(tc.b), tc.want; got != want {
			t.Errorf("Time(%d).Sub(%d) failed: got=%d, want=%d", tc.a, tc.b, got, want)
		}
	}
}

func TestAfter(t *testing.T) {
	testDuration := 100 * Millisecond
	if got, want := <-After(testDuration), (Time{value: zx.Time(testDuration)}); got.value < want.value {
		t.Errorf("time.After(%d) failed: got=%d, want>=%d", testDuration, got.value, want.value)
	}
}

func TestMonotonicString(t *testing.T) {
	for _, tc := range []struct {
		mono int64
		want string
	}{
		{0, "m=+0.000000000"},
		{123456789, "m=+0.123456789"},
		{-123456789, "m=-0.123456789"},
		{123456789000, "m=+123.456789000"},
		{-123456789000, "m=-123.456789000"},
		{9e18, "m=+9000000000.000000000"},
		{-9e18, "m=-9000000000.000000000"},
		{-1 << 63, "m=-9223372036.854775808"},
	} {
		if got, want := Monotonic(tc.mono).String(), tc.want; got != want {
			t.Errorf("with mono=%d: got %q; want %q", tc.mono, got, want)
		}
	}
}
