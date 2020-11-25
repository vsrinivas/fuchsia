// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bloaty

import (
	"io/ioutil"
	"path/filepath"
	"testing"
)

func TestGetFiles(t *testing.T) {
	const ids = `0071116f87e52dab0c21bd1cabc590fab5b06348 /out/libc.so
01885627175ee9996b08af6c64c95a6e30787269 /out/libpc-ps2.so
`
	idsPath := mkTempFile(t, ids)
	actual, errConfig := getFiles(idsPath)
	if errConfig != nil {
		t.Fatal(errConfig)
	}

	expected := []string{"/out/libc.so", "/out/libpc-ps2.so"}

	if len(actual) != len(expected) {
		t.Fatalf("In TestGenConfig, expected \n%s but got \n%s", expected, actual)
	}

	for i, val := range expected {
		if actual[i] != val {
			t.Fatalf("In TestGenConfig, expected \n%s but got \n%s", expected, actual)
		}
	}
}

func TestGetTopNFiles(t *testing.T) {
	input := map[string]*Segment{
		"LOAD [R]": {
			Files: map[string]*File{
				"file.c": {
					TotalFilesz: 14,
					Symbols: map[string]*Symbol{
						"ecm_bind": {
							Name:     "ecm_bind",
							Vmsz:     14,
							Filesz:   14,
							Binaries: []string{"lib.so"},
						},
					},
				},
				"different.c": {
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
					TotalFilesz: 1,
					Symbols: map[string]*Symbol{
						"ecm_bind": {
							Name:     "ecm_bind",
							Vmsz:     1,
							Filesz:   1,
							Binaries: []string{"lib.so"},
						},
					},
				},
				"test.c": {
					TotalFilesz: 4,
					Symbols: map[string]*Symbol{
						"test": {
							Name:     "test",
							Vmsz:     4,
							Filesz:   4,
							Binaries: []string{"lib.so"},
						},
					},
				},
			},
		},
	}

	fileSizes := map[string]uint64{
		"file.c":      14,
		"different.c": 5,
		"other.c":     1,
		"test.c":      4,
	}

	getTopN(fileSizes, 1, 0, &input)
	if len(input) != 2 {
		t.Fatalf("In TestGetTopN, len is wrong: \n%+v", input)
	}

	if _, ok := input["LOAD [R]"]; !ok {
		t.Fatalf("In TestGetTopN, missing LOAD [R]: \n%+v", input)
	}

	if len(input["LOAD [R]"].Files) != 2 {
		t.Fatalf("In TestGetTopN, len is wrong: \n%+v", input["LOAD [R]"].Files)
	}

	if val, ok := input["LOAD [R]"].Files["file.c"]; !ok {
		t.Fatalf("In TestGetTopN, missing file.c: \n%+v", input["LOAD [R]"].Files)
	} else if val.TotalFilesz != 14 {
		t.Fatalf("In TestGetTopN, filesz is wrong: \n%+v", val)
	}

	if val, ok := input["LOAD [R]"].Files["all small files"]; !ok {
		t.Fatalf("In TestGetTopN, missing all small files: \n%+v", input["LOAD [R]"].Files)
	} else if val.TotalFilesz != 5 {
		t.Fatalf("In TestGetTopN, filesz is wrong: \n%+v", val)
	}

	if _, ok := input["LOAD [RX]"]; !ok {
		t.Fatalf("In TestGetTopN, missing LOAD [RX]: \n%+v", input)
	}

	if len(input["LOAD [RX]"].Files) != 1 {
		t.Fatalf("In TestGetTopN, len is wrong: \n%+v", input["LOAD [R]"].Files)
	}

	if val, ok := input["LOAD [RX]"].Files["all small files"]; !ok {
		t.Fatalf("In TestGetTopN, missing all small files: \n%+v", input["LOAD [R]"].Files)
	} else if val.TotalFilesz != 5 {
		t.Fatalf("In TestGetTopN, filesz is wrong: \n%+v", val)
	}
}

func TestGetTopNSymbols(t *testing.T) {
	input := map[string]*Segment{
		"LOAD [R]": {
			Files: map[string]*File{
				"file.c": {
					TotalFilesz: 14,
					Symbols: map[string]*Symbol{
						"ecm_bind": {
							Name:     "ecm_bind",
							Vmsz:     14,
							Filesz:   14,
							Binaries: []string{"lib.so"},
						},
						"test": {
							Name:     "test",
							Vmsz:     23,
							Filesz:   5,
							Binaries: []string{"lib.so"},
						},
						"other": {
							Name:     "other",
							Vmsz:     5,
							Filesz:   5,
							Binaries: []string{"lib.so"},
						},
					},
				},
			},
		},
	}

	fileSizes := map[string]uint64{
		"file.c":      14,
		"different.c": 5,
		"other.c":     1,
		"test.c":      4,
	}

	getTopN(fileSizes, 0, 1, &input)
	if len(input) != 1 {
		t.Fatalf("In TestGetTopNSymbols, len is wrong: \n%+v", input)
	}

	if _, ok := input["LOAD [R]"]; !ok {
		t.Fatalf("In TestGetTopNSymbols, missing LOAD [R]: \n%+v", input)
	}

	if len(input["LOAD [R]"].Files) != 1 {
		t.Fatalf("In TestGetTopNSymbols, len is wrong: \n%+v", input["LOAD [R]"].Files)
	}

	if val, ok := input["LOAD [R]"].Files["file.c"]; !ok {
		t.Fatalf("In TestGetTopNSymbols, missing file.c: \n%+v", input["LOAD [R]"].Files)
	} else if val.TotalFilesz != 14 {
		t.Fatalf("In TestGetTopNSymbols, filesz is wrong: \n%+v", val)
	}

	if len(input["LOAD [R]"].Files["file.c"].Symbols) != 2 {
		t.Fatalf("In TestGetTopNSymbols, len is wrong: \n%+v", input["LOAD [R]"].Files["file.c"].Symbols)
	}

	if val, ok := input["LOAD [R]"].Files["file.c"].Symbols["ecm_bind"]; !ok {
		t.Fatalf("In TestGetTopNSymbols, missing ecm_bind: \n%+v", input["LOAD [R]"].Files)
	} else if val.Filesz != 14 {
		t.Fatalf("In TestGetTopNSymbols, filesz is wrong: \n%+v", val)
	}

	if val, ok := input["LOAD [R]"].Files["file.c"].Symbols["all small syms"]; !ok {
		t.Fatalf("In TestGetTopNSymbols, missing ecm_bind: \n%+v", input["LOAD [R]"].Files["file.c"].Symbols["all small syms"])
	} else if val.Filesz != 10 {
		t.Fatalf("In TestGetTopNSymbols, filesz is wrong: \n%+v", val)
	}
}

func TestAddRowToOutput(t *testing.T) {
	rows := []row{
		{"ecm_bind", "other.c", "LOAD [RX]", 7, 7},
		{"test", "other.c", "LOAD [RX]", 12, 2},
		{"ecm_bind", "other.c", "LOAD [R]", 23, 5},
		{"ecm_bind", "file.c", "LOAD [R]", 3, 3},
	}

	actual := make(map[string]*Segment)
	for _, row := range rows {
		addRowToOutput(&row, row.File, actual)
	}

	// {"ecm_bind", "other.c", "LOAD [RX]", 7, 7},
	if _, ok := actual["LOAD [RX]"]; !ok {
		t.Fatalf("In TestAddRowToOutput, got \n%+v", actual)
	}
	if _, ok := actual["LOAD [RX]"].Files["other.c"]; !ok {
		t.Fatalf("In TestAddRowToOutput, missing LOAD[RX] other.c")
	}
	if val, ok := actual["LOAD [RX]"].Files["other.c"].Symbols["ecm_bind"]; !ok {
		t.Fatalf("In TestAddRowToOutput, missing LOAD[RX] other.c ecm_bind")
	} else if val.Name != "ecm_bind" || val.Vmsz != 7 || val.Filesz != 7 {
		t.Fatalf("In TestAddRowToOutput, got \n%+v", val)
	}

	// {"test", "other.c", "LOAD [RX]", 12, 2},
	if val, ok := actual["LOAD [RX]"].Files["other.c"].Symbols["test"]; !ok {
		t.Fatalf("In TestAddRowToOutput, missing LOAD[RX] other.c test")
	} else if val.Name != "test" || val.Vmsz != 12 || val.Filesz != 2 {
		t.Fatalf("In TestAddRowToOutput, got \n%+v", val)
	}

	// {"ecm_bind", "other.c", "LOAD [R]", 23, 5},
	if _, ok := actual["LOAD [R]"]; !ok {
		t.Fatalf("In TestAddRowToOutput, missing LOAD[R]")
	}
	if _, ok := actual["LOAD [R]"].Files["other.c"]; !ok {
		t.Fatalf("In TestAddRowToOutput, missing LOAD[R]:other.c")
	}
	if val, ok := actual["LOAD [R]"].Files["other.c"].Symbols["ecm_bind"]; !ok {
		t.Fatalf("In TestAddRowToOutput, missing LOAD[R] other.c ecm_bind")
	} else if val.Name != "ecm_bind" || val.Vmsz != 23 || val.Filesz != 5 {
		t.Fatalf("In TestAddRowToOutput, got \n%+v", val)
	}

	// {"ecm_bind", "file.c", "LOAD [R]", 3, 3},
	if _, ok := actual["LOAD [R]"].Files["file.c"]; !ok {
		t.Fatalf("In TestAddRowToOutput, missing LOAD[R] file.c")
	}
	if val, ok := actual["LOAD [R]"].Files["file.c"].Symbols["ecm_bind"]; !ok {
		t.Fatalf("In TestAddRowToOutput, missing LOAD[R] file.c ecm_bind")
	} else if val.Name != "ecm_bind" || val.Vmsz != 3 || val.Filesz != 3 {
		t.Fatalf("In TestAddRowToOutput, got \n%+v", val)
	}
}

// mkTempFile returns a new temporary file with the specified content that will
// be cleaned up automatically.
func mkTempFile(t *testing.T, content string) string {
	name := filepath.Join(t.TempDir(), "foo")
	if err := ioutil.WriteFile(name, []byte(content), 0o600); err != nil {
		t.Fatal(err)
	}
	return name
}
