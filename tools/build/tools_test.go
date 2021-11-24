// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"testing"
)

func TestLookupPath(t *testing.T) {
	tools := Tools{
		{Name: "foo", OS: "linux", CPU: "x64", Path: "linux_x64/foo"},
		{Name: "foo", OS: "linux", CPU: "arm64", Path: "linux_arm64/foo"},
		{Name: "foo", OS: "mac", CPU: "x64", Path: "mac_x64/foo"},
		{Name: "linux-only", OS: "linux", CPU: "x64", Path: "linux_x64/linux-only"},
	}

	testCases := []struct {
		name     string
		tool     string
		platform string
		wantPath string
		wantErr  bool
	}{
		{
			name:     "linux tool",
			tool:     "linux-only",
			platform: "linux-x64",
			wantPath: "linux_x64/linux-only",
		},
		{
			name:     "mac tool",
			tool:     "foo",
			platform: "mac-x64",
			wantPath: "mac_x64/foo",
		},
		{
			name:     "linux arm64",
			tool:     "foo",
			platform: "linux-arm64",
			wantPath: "linux_arm64/foo",
		},
		{
			name:     "missing",
			tool:     "does-not-exist",
			platform: "linux-x64",
			wantErr:  true,
		},
		{
			name:     "unsupported os",
			tool:     "linux-only",
			platform: "mac-x64",
			wantErr:  true,
		},
		{
			name:     "unsupported arch",
			tool:     "linux-only",
			platform: "linux-arm64",
			wantErr:  true,
		},
	}
	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			gotPath, err := tools.LookupPath(tc.platform, tc.tool)
			if err != nil {
				if !tc.wantErr {
					t.Fatalf("Got unexpected error: %s", err)
				}
			} else if tc.wantErr {
				t.Fatal("Expected error but got nil")
			}

			if gotPath != tc.wantPath {
				t.Errorf("Wanted path %q, got %q", tc.wantPath, gotPath)
			}
		})
	}
}
