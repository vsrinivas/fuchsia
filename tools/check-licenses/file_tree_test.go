// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"context"
	"os"
	"path/filepath"
	"testing"
)

func TestFileTreeNew(t *testing.T) {
	baseDir := filepath.Join(*testDataDir, "filetree", "simple")
	configPath := filepath.Join(*testDataDir, "filetree", "simple.json")
	testFile := filepath.Join(*testDataDir, "filetree", "simple", "test.py")

	config := Config{}
	if err := config.Init(configPath); err != nil {
		t.Fatal(err)
	}
	config.BaseDir = baseDir

	metrics := Metrics{}
	metrics.Init()
	got := NewFileTree(context.Background(), config.BaseDir, nil, &config, &metrics)

	cwd, err := os.Getwd()
	if err != nil {
		t.Error(err)
	}
	want := &FileTree{
		Name: "simple",
		Path: filepath.Join(cwd, baseDir),
	}
	f, err := NewFile(testFile, got)
	if err != nil {
		t.Error(err)
	}
	want.Files = append(want.Files, f)

	if !got.Equal(want) {
		t.Errorf("%v(): got %v, want %v", t.Name(), got, want)
	}
}

func TestFileTreeHasLowerPrefix(t *testing.T) {
	name := "LICENSE-THIRD-PARTY"
	singleLicenseFiles := []string{"license", "readme"}
	if !hasLowerPrefix(name, singleLicenseFiles) {
		t.Errorf("%v: %v is not a single license file", t.Name(), name)
	}
}
