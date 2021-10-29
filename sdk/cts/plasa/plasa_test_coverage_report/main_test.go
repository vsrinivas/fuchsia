// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

// This program converts a directory of YAML reports produced by `clang_doc` into
// a report usable by test coverage.
//
// Please refer to the file README.md in this directory for more information
// about the program and its use.

package main

import (
	"flag"
	"os"
	"path"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
)

var testDir = flag.String("test_data_dir", "", "The directory where test data reside")

func TestTestDir(t *testing.T) {
	if *testDir == "" {
		t.Fatalf("the required flag --test_data_dir=... was not supplied")
	}
}

func TestPlasaManifestRead(t *testing.T) {
	expected := `{
    "items": [
        {
            "name": "::ns::foo1",
            "kind": "api_cc"
        },
        {
            "name": "::ns::foo2",
            "kind": "api_cc"
        },
        {
            "name": "fuchsia.library/Protocol.member",
            "kind": "api_fidl"
        }
    ]
}
`
	manifest := path.Join(*testDir, "plasa.manifest.json")
	m, err := os.Open(manifest)
	if err != nil {
		t.Fatalf("could not open: %v: %v", manifest, err)
	}
	var s strings.Builder
	if err := filter(m, &s); err != nil {
		t.Fatalf("while running manifest check: %v", err)
	}
	la := strings.Split(s.String(), "\n")
	le := strings.Split(expected, "\n")
	if !cmp.Equal(la, le) {
		t.Errorf("want:\n%v,\n\ngot:\n%v\n\ndiff:\n%v",
			strings.Join(le, "\n"), strings.Join(la, "\n"), cmp.Diff(le, la))
	}
}
