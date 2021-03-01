// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package device

import (
	"testing"
)

func TestTranslateToOldNodeName(t *testing.T) {
	type test struct {
		src string
		dst string
	}

	tests := []test{
		{
			src: "fuchsia-0000-0000-0000",
			dst: "acid-acid-acid-acid",
		},
		{
			src: "fuchsia-aaaa-aaaa-aaaa",
			dst: "cable-cable-cable-cable",
		},
		{
			src: "fuchsia-ffff-ffff-ffff",
			dst: "clock-clock-clock-clock",
		},
		{
			src: "fuchsia-0123-4567-890a",
			dst: "slip-acts-pupil-bony",
		},
		{
			src: "fuchsia-fedc-ba98-7654",
			dst: "gong-whiff-most-busy",
		},
	}

	for _, test := range tests {
		name, err := translateToOldNodeName(test.src)
		if err != nil {
			t.Fatal(err)
		}
		if test.dst != name {
			t.Fatalf("for %q, expected %q, but got %q", test.src, test.dst, name)
		}
	}
}
