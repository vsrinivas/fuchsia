// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package jsonutil

import (
	"path/filepath"
	"testing"

	"github.com/google/go-cmp/cmp"
)

type testStruct struct {
	Integer int    `json:"integer"`
	Str     string `json:"str"`
}

func TestReadWriteRoundTrip(t *testing.T) {
	original := []testStruct{
		{Integer: 5, Str: "foo"},
		{Integer: 6, Str: "bar"},
	}

	path := filepath.Join(t.TempDir(), "file.json")
	if err := WriteToFile(path, original); err != nil {
		t.Fatal(err)
	}

	var got []testStruct
	if err := ReadFromFile(path, &got); err != nil {
		t.Fatal(err)
	}

	if diff := cmp.Diff(original, got); diff != "" {
		t.Fatalf("round-trip file read/write returned wrong JSON (-want +got):\n%s", diff)
	}
}
