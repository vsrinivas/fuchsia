// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package golden

import (
	"flag"
	"io/ioutil"
	"os"
	"path"
	"path/filepath"
	"regexp"
	"strings"
	"testing"
)

var testDataFlag = flag.String("test_data_dir", "", "Path to testdata")

func TestPatternsMatchExamples(t *testing.T) {
	goldenDir := path.Join(filepath.Dir(os.Args[0]), *testDataFlag)
	wantDirs := map[string]string{"examples": "examples", "patterns": "patterns"}
	base := goldenDir + "/"
	for folder, _ := range wantDirs {
		if _, err := os.Stat(base + folder); os.IsNotExist(err) {
			t.Errorf("%v(%v doesn't exist): got %v, want %v", t.Name(), base+folder, err, nil)
		}
	}
	example_files, err := ioutil.ReadDir(base + wantDirs["examples"])
	if err != nil {
		t.Errorf("%v, got %v", t.Name(), err)
	}
	pattern_files, err := ioutil.ReadDir(base + wantDirs["patterns"])
	if err != nil {
		t.Errorf("%v, got %v", t.Name(), err)
	}
	if len(example_files) != len(pattern_files) {
		t.Errorf("%v, got %v != %v", t.Name(), len(example_files), len(pattern_files))
	}
	for _, pattern_file := range pattern_files {
		path := base + wantDirs["patterns"] + "/" + pattern_file.Name()
		pattern, err := ioutil.ReadFile(path)
		if err != nil {
			t.Errorf("%v, got %v", t.Name(), err)
		}
		basename := strings.TrimSuffix(pattern_file.Name(), filepath.Ext(pattern_file.Name()))
		example_file := basename + ".txt"
		path = base + wantDirs["examples"] + "/" + example_file
		example, err := ioutil.ReadFile(path)
		if err != nil {
			t.Errorf("%v, got %v", t.Name(), err)
		}
		re := regexp.MustCompile(string(pattern))
		if !re.Match(example) {
			t.Errorf("%v, %v pattern doesn't match example", t.Name(), basename)
		}
	}
}
