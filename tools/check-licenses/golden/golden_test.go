// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package golden

import (
	"flag"
	"io/ioutil"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"testing"
)

var testDataFlag = flag.String("test_data_dir", "", "Path to test data; only used in GN build")

func TestPatternsMatchExamples(t *testing.T) {
	wantDirs := []string{"examples", "patterns"}
	for _, folder := range wantDirs {
		p := filepath.Join(*testDataFlag, folder)
		if _, err := os.Stat(p); os.IsNotExist(err) {
			t.Errorf("%v(%v doesn't exist): got %v, want %v", t.Name(), p, err, nil)
		}
	}
	example_files, err := ioutil.ReadDir(filepath.Join(*testDataFlag, "examples"))
	if err != nil {
		t.Errorf("%v, got %v", t.Name(), err)
	}
	pattern_files, err := ioutil.ReadDir(filepath.Join(*testDataFlag, "patterns"))
	if err != nil {
		t.Errorf("%v, got %v", t.Name(), err)
	}
	if len(example_files) != len(pattern_files) {
		t.Errorf("%v, got %v != %v", t.Name(), len(example_files), len(pattern_files))
	}
	for _, pattern_file := range pattern_files {
		pattern, err := ioutil.ReadFile(filepath.Join(*testDataFlag, "patterns", pattern_file.Name()))
		if err != nil {
			t.Errorf("%v, got %v", t.Name(), err)
		}
		example_file := strings.TrimSuffix(pattern_file.Name(), filepath.Ext(pattern_file.Name())) + ".txt"
		example, err := ioutil.ReadFile(filepath.Join(*testDataFlag, "examples", example_file))
		if err != nil {
			t.Errorf("%v, got %v", t.Name(), err)
		}
		updatedRegex := strings.ReplaceAll(string(pattern), "\n", `[\s\\#\*\/]*`)
		updatedRegex = strings.ReplaceAll(updatedRegex, " ", `[\s\\#\*\/]*`)
		if !regexp.MustCompile(updatedRegex).Match(example) {
			t.Errorf("%v, %v pattern doesn't match example", t.Name(), pattern_file.Name())
		}
	}
}
