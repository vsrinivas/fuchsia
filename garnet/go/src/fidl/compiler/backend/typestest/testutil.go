// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package typestest

import (
	"fmt"
	"io/ioutil"
	"path/filepath"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/types"
)

// AllExamples returns all examples by filename.
// See also #GetExample.
func AllExamples(basePath string) []string {
	// Trim spurious trailing slash.
	basePath = strings.TrimRight(basePath, "/")
	paths, err := filepath.Glob(filepath.Join(basePath, "*.json"))
	if err != nil {
		panic(err)
	}
	if len(paths) == 0 {
		panic("Wrong. There should be a few JSON golden.")
	}
	examples := make([]string, 0, len(paths))
	for _, path := range paths {
		examples = append(examples, path[len(basePath)+1:])
	}
	return examples
}

// GetExample retrieves an example by filename, and parses it.
func GetExample(basePath, filename string) types.Root {
	// Trim spurious trailing slash.
	basePath = strings.TrimRight(basePath, "/")
	if strings.HasPrefix(filename, "/") {
		panic(fmt.Sprintf("ensure filename doesn't have / prefix: %q", filename))
	}
	fidl, err := types.ReadJSONIr(filepath.Join(basePath, filename))
	if err != nil {
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
func AssertCodegenCmp(t *testing.T, want, got []byte) {
	if len(want) == 0 && len(got) != 0 {
		t.Fatalf("generated code was unexpectedly empty")
	}
	splitWant := strings.Split(strings.TrimSpace(string(want)), "\n")
	splitGot := strings.Split(strings.TrimSpace(string(got)), "\n")
	if diff := cmp.Diff(splitWant, splitGot); diff != "" {
		t.Errorf("unexpected difference (-want/+got):\n%s", diff)
	}
}
