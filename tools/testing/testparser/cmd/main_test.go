// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"encoding/json"
	"testing"

	"github.com/google/go-cmp/cmp"
)

func compactJson(jsonBytes []byte) []byte {
	buffer := bytes.NewBuffer([]byte{})
	json.Compact(buffer, jsonBytes)
	return buffer.Bytes()
}

func TestIndentJSON(t *testing.T) {
	want := `[
	{
		"display_name": "tests::ignored_test",
		"suite_name": "tests",
		"case_name": "ignored_test",
		"status": "Skip",
		"duration_nanos": 0,
		"format": "Rust"
	},
	{
		"display_name": "tests::test_add_hundred",
		"suite_name": "tests",
		"case_name": "test_add_hundred",
		"status": "Pass",
		"duration_nanos": 0,
		"format": "Rust"
	},
	{
		"display_name": "tests::test_add",
		"suite_name": "tests",
		"case_name": "test_add",
		"status": "Fail",
		"duration_nanos": 0,
		"format": "Rust"
	}
]`
	result, err := indentJSON(compactJson([]byte(want)))
	if err != nil {
		t.Errorf("TestIndentJSON failed: %v", err)
	}
	if diff := cmp.Diff(string(result), want); diff != "" {
		t.Errorf("indentJSON returned wrong output(diff): %s\n", diff)
	}
}
