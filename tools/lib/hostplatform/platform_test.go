// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package hostplatform

import (
	"testing"
)

func TestName(t *testing.T) {
	testCases := []struct {
		goOS      string
		goArch    string
		want      string
		expectErr bool
	}{
		{
			goOS:   "darwin",
			goArch: "amd64",
			want:   "mac-x64",
		},
		{
			goOS:   "linux",
			goArch: "arm64",
			want:   "linux-arm64",
		},
		{
			goOS:   "windows",
			goArch: "amd64",
			want:   "win-x64",
		},
		{
			goOS:      "android",
			goArch:    "arm64",
			expectErr: true,
		},
		{
			goOS:      "linux",
			goArch:    "386",
			expectErr: true,
		},
	}

	for _, tc := range testCases {
		t.Run(tc.goOS+" "+tc.goArch, func(t *testing.T) {
			got, err := name(tc.goOS, tc.goArch)
			if err != nil {
				if !tc.expectErr {
					t.Fatal(err)
				}
			} else if tc.expectErr {
				t.Errorf("expected name(%s, %s) to fail but got %s", tc.goOS, tc.goArch, got)
			}
			if got != tc.want {
				t.Errorf("name(%s, %s) = %s is wrong, wanted %s", tc.goOS, tc.goArch, got, tc.want)
			}
		})
	}
}
