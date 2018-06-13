// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bloaty

import (
	"bytes"
	"testing"
)

const (
	tsv = `segments	compileunits	symbols	vmsize	filesize
[Unmapped]	file.c	[section .debug_info]	0	15550
LOAD [RX]	other.c	ecm_bind	4142	4142
[ELF Headers]			0	1920
LOAD [RX]	[LOAD [RX]]		530	0`
)

func TestReadCSV(t *testing.T) {
	reader := bytes.NewReader([]byte(tsv))
	rows := make(chan row)
	go func() {
		err := ReadCSV(reader, rows)
		if err != nil {
			t.Fatal(err)
		}
	}()

	actual := []row{}
	for r := range rows {
		actual = append(actual, r)
	}

	expected := []row{
		{"ecm_bind", "other.c", "LOAD [RX]", 4142, 4142},
	}

	if len(actual) != len(expected) {
		t.Fatalf("In TestReadCSV, expected \n%v but got \n%v", expected, actual)
	}

	for i, val := range expected {
		if actual[i] != val {
			t.Fatalf("In TestReadCSV, expected \n%v but got \n%v", expected, actual)
		}
	}
}

func TestFilterRow(t *testing.T) {
	goodRow := row{"ecm_bind", "other.c", "LOAD [RX]", 4142, 4142}
	badRow := row{"[section .debug_info]", "file.c", "[Unmapped]", 0, 15550}

	actual := filterRow(goodRow)
	expected := true

	if actual != expected {
		t.Fatalf("In TestFilterRows, expected \n%v but got \n%v", expected, actual)
	}

	actual = filterRow(badRow)
	expected = false

	if actual != expected {
		t.Fatalf("In TestFilterRows, expected \n%v but got \n%v", expected, actual)
	}
}
