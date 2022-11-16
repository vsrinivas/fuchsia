// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package util

import (
	"encoding/json"
	"flag"
	"os"
	"path/filepath"
	"testing"

	"github.com/google/go-cmp/cmp"
)

var (
	testDataDir = flag.String("test_data_dir", "", "Path to test data directory")
	gnPath      = flag.String("gn_path", "", "Path to gn executable")
	buildDir    = flag.String("build_dir", "", "Path to out directory")
)

func TestFilterTargetsEmpty(t *testing.T) {
	projectJson := filepath.Join(*testDataDir, "project.json")
	_, err := NewGen(projectJson)
	if err == nil {
		t.Errorf("%v: expected error (Unable to find target in gen map), got nothing (project.json: %v).", t.Name(), projectJson)
	}
}

func TestFilterTargets(t *testing.T) {
	root := filepath.Join(*testDataDir, "example")
	projectJson := filepath.Join(root, "project.json")
	gen, err := NewGen(projectJson)
	if err != nil {
		t.Fatalf("%v: expected no error, (project.json: %v) got %v.", t.Name(), projectJson, err)
	}

	target := "//tools/check-licenses/util/testdata/example:example"
	err = gen.FilterTargets(target)
	if err != nil {
		t.Fatalf("%v: expected no error, (target: %v) got %v.", t.Name(), target, err)
	}

	want := loadWantJSON(filepath.Join(root, "want.json"), t)

	// No need to verify target.Children fields.
	for _, ft := range gen.FilteredTargets {
		ft.Children = nil
	}

	if d := cmp.Diff(want, gen.FilteredTargets); d != "" {
		t.Errorf("%v: compare Gens mismatch: (-want +got):\n%s", t.Name(), d)
	}
}

func loadWantJSON(wantFile string, t *testing.T) map[string]*Target {
	wantFileContent, err := os.ReadFile(wantFile)
	if err != nil {
		t.Fatalf("%v: failed to read in want.json file [%v]: %v\n", t.Name(), wantFile, err)
	}

	var want map[string]*Target
	err = json.Unmarshal(wantFileContent, &want)
	if err != nil {
		t.Fatalf("%v: failed to unmarshal want.json data [%v]: %v\n", t.Name(), wantFile, err)
	}
	return want
}
