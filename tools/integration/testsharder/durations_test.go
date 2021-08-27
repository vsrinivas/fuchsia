// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testsharder

import (
	"testing"
	"time"

	"go.fuchsia.dev/fuchsia/tools/build"
)

func TestTestDurationsMap(t *testing.T) {
	m := NewTestDurationsMap([]build.TestDuration{
		{
			Name:           defaultDurationKey,
			MedianDuration: 1,
		},
		{
			Name:           "foo.cmx",
			MedianDuration: 2,
		},
		{
			Name:           "bar",
			MedianDuration: 3,
		},
	})

	assertDurationEquals := func(name string, expected time.Duration) {
		duration := m.Get(Test{Test: build.Test{Name: name}})
		if duration.MedianDuration != expected {
			t.Fatalf("wrong duration for test %q: got %s, want %s", name, duration, expected)
		}
	}

	assertDurationEquals("unknown-test", 1)
	assertDurationEquals("foo.cmx", 2)
	// Should translate new-style legacy component test names to old-style.
	assertDurationEquals("foo.cm", 2)
	assertDurationEquals("bar", 3)
}
