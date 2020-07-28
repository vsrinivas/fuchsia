// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package affectedtests

import (
	"encoding/json"
	"flag"
	"io/ioutil"
	"path/filepath"
	"reflect"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/build"
)

var testDataFlag = flag.String("test_data_dir", "testdata", "Path to testdata/; only used in GN build")

func TestCoreDot(t *testing.T) {
	srcs := []string{"garnet/bin/log_listener/src/main.rs"}
	testsJSONContents, err := ioutil.ReadFile(filepath.Join(*testDataFlag, "core", "tests.json"))
	if err != nil {
		t.Fatal(err)
	}
	var testSpecs []build.TestSpec
	if err = json.Unmarshal(testsJSONContents, &testSpecs); err != nil {
		t.Fatal(err)
	}
	dotFileContents, err := ioutil.ReadFile(filepath.Join(*testDataFlag, "core", "ninja.dot"))
	if err != nil {
		t.Fatal(err)
	}

	want := []string{
		"fuchsia-pkg://fuchsia.com/log_listener_tests#meta/log_listener_bin_test.cmx",
		"fuchsia-pkg://fuchsia.com/log_listener_tests#meta/log_listener_return_code_test.cmx",
	}

	actual := AffectedTests(srcs, testSpecs, dotFileContents)
	if !reflect.DeepEqual(want, actual) {
		t.Errorf("AffectedTests(...) = %v; want %v`", actual, want)
	}
}
