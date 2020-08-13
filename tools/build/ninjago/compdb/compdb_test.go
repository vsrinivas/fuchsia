// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package compdb

import (
	"reflect"
	"strings"
	"testing"
)

func TestParseSimple(t *testing.T) {
	const testCase = `[
  {
    "directory": "out",
    "command": "touch file.stamp",
    "file": "file",
    "output": "file.stamp"
  }
]
`
	c, err := Parse(strings.NewReader(testCase))
	if err != nil {
		t.Errorf("Parse(%q)=_, %v; want=_, <nil>", testCase, err)
	}

	want := []Command{
		{
			Directory: "out",
			Command:   "touch file.stamp",
			File:      "file",
			Output:    "file.stamp",
		},
	}
	if !reflect.DeepEqual(c, want) {
		t.Errorf("Parse()=%v; want=%v", c, want)
	}
}
