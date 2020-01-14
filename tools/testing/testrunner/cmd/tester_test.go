// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"reflect"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/build/lib"
)

func TestSetCommand(t *testing.T) {
	cases := []struct {
		name        string
		test        build.Test
		useRuntests bool
		expected    []string
	}{
		{
			name:        "specified command is respected",
			useRuntests: false,
			test: build.Test{
				Path:    "/path/to/test",
				Command: []string{"a", "b", "c"},
			},
			expected: []string{"a", "b", "c"},
		},
		{
			name:        "use runtests",
			useRuntests: true,
			test: build.Test{
				Path:       "/path/to/test",
				PackageURL: "fuchsia-pkg://example.com/test.cmx",
			},
			expected: []string{"runtests", "-t", "test", "/path/to", "-o", "REMOTE_DIR"},
		},
		{
			name:        "system path",
			useRuntests: false,
			test: build.Test{
				Path: "/path/to/test",
			},
			expected: []string{"/path/to/test"},
		},
		{
			name:        "components v1",
			useRuntests: false,
			test: build.Test{
				Path:       "/path/to/test",
				PackageURL: "fuchsia-pkg://example.com/test.cmx",
			},
			expected: []string{"run-test-component", "fuchsia-pkg://example.com/test.cmx"},
		},
		{
			name:        "components v2",
			useRuntests: false,
			test: build.Test{
				Path:       "/path/to/test",
				PackageURL: "fuchsia-pkg://example.com/test.cm",
			},
			expected: []string{"run-test-suite", "fuchsia-pkg://example.com/test.cm"},
		},
	}

	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			setCommand(&c.test, c.useRuntests, "REMOTE_DIR")
			if !reflect.DeepEqual(c.test.Command, c.expected) {
				t.Errorf("unexpected command:\nexpected: %q\nactual: %q\n", c.expected, c.test.Command)
			}
		})

	}
}
