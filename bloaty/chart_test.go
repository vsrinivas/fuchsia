// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bloaty

import (
	"testing"
)

func TestToChart(t *testing.T) {
	input := map[string]*Segment{
		"LOAD [R]": {
			Files: map[string]*File{
				"file.c": {
					TotalFilesz: 3,
					Symbols: map[string]*Symbol{
						"ecm_bind": {
							Name:     "ecm_bind",
							Vmsz:     3,
							Filesz:   3,
							Binaries: []string{"lib.so"},
						},
					},
				},
				"other.c": {
					TotalFilesz: 5,
					Symbols: map[string]*Symbol{
						"ecm_bind": {
							Name:     "ecm_bind",
							Vmsz:     23,
							Filesz:   5,
							Binaries: []string{"lib.so"},
						},
					},
				},
			},
		},
		"LOAD [RX]": {
			Files: map[string]*File{
				"other.c": {
					TotalFilesz: 14,
					Symbols: map[string]*Symbol{
						"ecm_bind": {
							Name:     "ecm_bind",
							Vmsz:     7,
							Filesz:   7,
							Binaries: []string{"lib.so"},
						},
						"test": {
							Name:     "test",
							Vmsz:     12,
							Filesz:   7,
							Binaries: []string{"lib.so"},
						},
					},
				},
			},
		},
	}

	actual := *toTable(input)

	if len(actual) != 11 {
		t.Fatalf("In TestToChart, len is wrong: \n%+v", actual)
	}
	if len(actual[0]) != 3 && actual[0][0] != "ID" && actual[0][1] != "Parent" && actual[0][2] != "File Size" {
		t.Fatalf("In TestToChart, missing header: \n%+v", actual)
	}
	if len(actual[1]) != 3 && actual[1][0] != "Build" && actual[1][1] != nil && actual[1][2] != 0 {
		t.Fatalf("In TestToChart, missing header: \n%+v", actual)
	}
}
