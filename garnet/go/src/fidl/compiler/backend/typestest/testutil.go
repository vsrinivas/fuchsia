// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package typestest

import (
	"encoding/json"
	"io/ioutil"
	"path/filepath"
	"strings"
	"testing"

	"fidl/compiler/backend/types"

	"github.com/google/go-cmp/cmp"
)

// AllExamples returns all examples by filename.
// See also #GetExample.
func AllExamples(basePath string) []string {
	paths, err := filepath.Glob(filepath.Join(basePath, "*.json"))
	if err != nil {
		panic(err)
	}
	if len(paths) == 0 {
		panic("Wrong. There should be a few JSON golden.")
	}
	var examples []string
	for _, path := range paths {
		examples = append(examples, path[len(basePath):])
	}
	return examples
}

// GetExample retrieves an example by filename, and parses it.
func GetExample(basePath, filename string) types.Root {
	var (
		data = GetGolden(basePath, filename)
		fidl types.Root
	)
	if err := json.Unmarshal(data, &fidl); err != nil {
		panic(err)
	}
	return fidl
}

// GetGolden retrieves a golden example by filename, and returns the raw
// content.
func GetGolden(basePath, filename string) []byte {
	data, err := ioutil.ReadFile(filepath.Join(basePath, filename))
	if err != nil {
		panic(err)
	}
	return data
}

// AssertCodegenCmp assert that the actual codegen matches the expected codegen.
func AssertCodegenCmp(t *testing.T, expected, actual []byte) {
	var (
		splitExpected = strings.Split(strings.TrimSpace(string(expected)), "\n")
		splitActual   = strings.Split(strings.TrimSpace(string(actual)), "\n")
	)
	if diff := cmp.Diff(splitActual, splitExpected); diff != "" {
		t.Errorf("unexpected difference: %s\ngot: %s", diff, string(actual))
	}
}
