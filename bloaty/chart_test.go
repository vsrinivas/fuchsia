// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bloaty

import (
	"testing"
)

func checkRow(t *testing.T, row []interface{}, expected []interface{}) {
	if len(expected) != 4 {
		t.Fatalf("In TestToChart, len is wrong. expected: \n%+v\n but got: \n%+v", expected, row)
	}
	if row[0] != expected[0] {
		t.Fatalf("In TestToChart, len is wrong. expected: \n%+v\n but got: \n%+v", expected, row)
	}
	if len(row) != 4 {
		t.Fatalf("In TestToChart, id is wrong. expected: \n%+v\n but got: \n%+v", expected, row)
	}
	if row[1] != expected[1] {
		t.Fatalf("In TestToChart, parent is wrong. expected: \n%+v\n but got: \n%+v", expected, row)
	}
	if row[2] != expected[2] {
		t.Fatalf("In TestToChart, size is wrong. expected: \n%+v\n but got: \n%+v", expected, row)
	}
	if row[3] != expected[3] {
		t.Fatalf("In TestToChart, color is wrong. expected: \n%+v\n but got: \n%+v", expected, row)
	}
}

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
							Binaries: []string{"lib.so", "other.so"},
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
							Binaries: []string{"lib.so", "other.so"},
						},
					},
				},
			},
		},
	}

	actual := *toTable(input)

	ids_to_expected_rows := map[string][]interface{}{
		"ID":                           {"ID", "Parent", "File Size", "Color"},
		"Build":                        {"Build", nil, uint64(0), 0},
		"LOAD [R]":                     {"LOAD [R]", "Build", uint64(0), 0},
		"LOAD [RX]":                    {"LOAD [RX]", "Build", uint64(0), 0},
		"file.c (LOAD [R])":            {"file.c (LOAD [R])", "LOAD [R]", uint64(3), 0},
		"other.c (LOAD [R])":           {"other.c (LOAD [R])", "LOAD [R]", uint64(5), 0},
		"other.c (LOAD [RX])":          {"other.c (LOAD [RX])", "LOAD [RX]", uint64(14), 0},
		"file.c:ecm_bind (LOAD [R])":   {"file.c:ecm_bind (LOAD [R])", "file.c (LOAD [R])", uint64(3), 1},
		"other.c:ecm_bind (LOAD [R])":  {"other.c:ecm_bind (LOAD [R])", "other.c (LOAD [R])", uint64(5), 2},
		"other.c:ecm_bind (LOAD [RX])": {"other.c:ecm_bind (LOAD [RX])", "other.c (LOAD [RX])", uint64(7), 1},
		"other.c:test (LOAD [RX])":     {"other.c:test (LOAD [RX])", "other.c (LOAD [RX])", uint64(7), 2},
	}

	if len(actual) != 11 {
		t.Fatalf("In TestToChart, len is wrong: \n%+v", actual)
	}
	for _, row := range actual {
		if len(row) != 4 {
			t.Fatalf("In TestToChart, len is wrong: \n%+v", row)
		}
		checkRow(t, row, ids_to_expected_rows[row[0].(string)])
	}
}
