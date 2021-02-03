// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
)

func TestParseArgs(t *testing.T) {
	testCases := []struct {
		name      string
		args      []string
		expected  setArgs
		expectErr bool
	}{
		{
			name:      "missing PRODUCT.BOARD",
			args:      []string{},
			expectErr: true,
		},
		{
			name: "valid PRODUCT.BOARD",
			args: []string{"core.x64"},
			expected: setArgs{
				product: "core",
				board:   "x64",
			},
		},
		{
			name:      "invalid PRODUCT.BOARD",
			args:      []string{"corex64"},
			expectErr: true,
		},
		{
			name:      "multiple PRODUCT.BOARD args",
			args:      []string{"core.x64", "bringup.arm64"},
			expectErr: true,
		},
		{
			name: "verbose",
			args: []string{"--verbose", "core.x64"},
			expected: setArgs{
				product: "core",
				board:   "x64",
				verbose: true,
			},
		},
		{
			name: "universe packages",
			args: []string{"core.x64", "--with", "u1,u2", "--with", "u3,u4"},
			expected: setArgs{
				product:          "core",
				board:            "x64",
				universePackages: []string{"u1", "u2", "u3", "u4"},
			},
		},
		{
			name: "base packages",
			args: []string{"core.x64", "--with-base", "b1,b2", "--with-base", "b3,b4"},
			expected: setArgs{
				product:      "core",
				board:        "x64",
				basePackages: []string{"b1", "b2", "b3", "b4"},
			},
		},
		{
			name: "cache packages",
			args: []string{"core.x64", "--with-cache", "c1,c2", "--with-cache", "c3,c4"},
			expected: setArgs{
				product:       "core",
				board:         "x64",
				cachePackages: []string{"c1", "c2", "c3", "c4"},
			},
		},
		{
			name: "host labels",
			args: []string{"core.x64", "--with-host", "h1,h2", "--with-host", "h3,h4"},
			expected: setArgs{
				product:    "core",
				board:      "x64",
				hostLabels: []string{"h1", "h2", "h3", "h4"},
			},
		},
		{
			name: "variants",
			args: []string{"core.x64", "--variant", "kasan,profile", "--variant", "ubsan"},
			expected: setArgs{
				product:  "core",
				board:    "x64",
				variants: []string{"kasan", "profile", "ubsan"},
			},
		},
		{
			name: "gn args",
			args: []string{"core.x64", "--args", `foo=["bar", "baz"]`, "--args", "x=5"},
			expected: setArgs{
				product: "core",
				board:   "x64",
				// --args values shouldn't be split at commas, since commas can
				// be part of the args themselves.
				gnArgs: []string{`foo=["bar", "baz"]`, "x=5"},
			},
		},
		{
			name: "release",
			args: []string{"core.x64", "--release"},
			expected: setArgs{
				product:   "core",
				board:     "x64",
				isRelease: true,
			},
		},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			cmd, err := parseArgs(tc.args)
			if err != nil {
				if !tc.expectErr {
					t.Fatalf("Parse args error: %s", err)
				}
				return
			} else if tc.expectErr {
				t.Fatalf("Expected parse args error but parsing succeeded")
			}

			opts := []cmp.Option{cmpopts.EquateEmpty(), cmp.AllowUnexported(setArgs{})}
			if diff := cmp.Diff(&tc.expected, cmd, opts...); diff != "" {
				t.Fatalf("Unexpected arg parse result (-want +got):\n%s", diff)
			}
		})
	}
}
