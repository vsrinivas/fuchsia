// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package affectedtests

import (
	"compress/gzip"
	"encoding/json"
	"flag"
	"io/ioutil"
	"os"
	"path/filepath"
	"reflect"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/build"
)

var testDataFlag = flag.String("test_data_dir", "testdata", "Path to testdata/; only used in GN build")

func TestCoreDOT(t *testing.T) {
	jsonPath := filepath.Join(*testDataFlag, "core", "tests.json")
	testsJSONContents, err := ioutil.ReadFile(jsonPath)
	if err != nil {
		t.Fatalf("Failed to read file %q: %s", jsonPath, err)
	}
	var testSpecs []build.TestSpec
	if err = json.Unmarshal(testsJSONContents, &testSpecs); err != nil {
		t.Fatalf("Failed to unmarshal test JSON: %s", err)
	}
	dotPath := filepath.Join(*testDataFlag, "core", "ninja.dot.gz")

	for _, tc := range []struct {
		desc string
		srcs []string
		want []string
	}{
		{
			desc: "multiple affected tests",
			srcs: []string{"garnet/bin/log_listener/src/main.rs"},
			want: []string{
				"fuchsia-pkg://fuchsia.com/log_listener_tests#meta/log_listener_bin_test.cmx",
				"fuchsia-pkg://fuchsia.com/log_listener_tests#meta/log_listener_return_code_test.cmx",
			},
		},
		{
			desc: "change to node from the bottom of the DOT file",
			srcs: []string{"src/sys/component_index/src/main.rs"},
			want: []string{"fuchsia-pkg://fuchsia.com/component_index_tests#meta/component_index_tests.cmx"},
		},
	} {
		t.Run(tc.desc, func(t *testing.T) {
			dotFile, err := os.Open(dotPath)
			if err != nil {
				t.Fatalf("Failed to open file %q: %s", dotPath, err)
			}
			defer dotFile.Close()

			dotFileUnzipped, err := gzip.NewReader(dotFile)
			if err != nil {
				t.Fatalf("Failed to unzip file %q: %s", dotPath, err)
			}
			defer dotFileUnzipped.Close()

			actual, err := AffectedTests(tc.srcs, testSpecs, dotFileUnzipped)
			if err != nil {
				t.Fatalf("AffectedTests(%v, _, _) failed: %s", tc.srcs, err)
			}
			if !reflect.DeepEqual(tc.want, actual) {
				t.Errorf("AffectedTests(%v, _, _) = %v; want %v", tc.srcs, actual, tc.want)
			}
		})
	}
}
