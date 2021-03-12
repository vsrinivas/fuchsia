// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestAsMap(t *testing.T) {
	x86 := Tool{Name: "foo", OS: "linux", CPU: "x64"}
	arm := Tool{Name: "foo", OS: "linux", CPU: "arm64"}
	mac := Tool{Name: "foo", OS: "mac", CPU: "x64"}
	other := Tool{Name: "bar", OS: "linux", CPU: "x64"}

	got := Tools{x86, arm, mac, other}.AsMap("linux-x64")

	want := map[string]Tool{x86.Name: x86, other.Name: other}

	if diff := cmp.Diff(want, got); diff != "" {
		t.Fatalf("Got wrong tools map (-want +got):\n%s", diff)
	}
}
