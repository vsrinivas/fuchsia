// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package golden

import (
	"io/ioutil"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"testing"
)

func TestPatternsMatchExamples(t *testing.T) {
	// TODO(solomonkinard) this works locally, but CQ doesn't know about relative paths
	t.Skip()
	goldenDir := "../../tools/check-licenses/golden"
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
