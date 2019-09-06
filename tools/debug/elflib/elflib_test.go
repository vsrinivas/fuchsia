// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package elflib

import (
	"encoding/hex"
	"os"
	"path"
	"path/filepath"
	"testing"
)

func getTestdataPath(filename string) (string, error) {
	testPath, err := filepath.Abs(os.Args[0])
	if err != nil {
		return "", err
	}
	outDir := filepath.Dir(testPath)
	return path.Join(outDir, "testdata", "elflib", filename), nil
}

func TestBuildIDs(t *testing.T) {
	testfile, err := getTestdataPath("libc.elf.section-only")
	if err != nil {
		t.Fatal(err)
	}
	f, err := os.Open(testfile)
	if err != nil {
		t.Fatal("from os.Open: ", err)
	}
	buildIDs, err := GetBuildIDs(testfile, f)
	if err != nil {
		t.Fatal("from GetBuildIDs: ", err)
	}
	if len(buildIDs) != 1 {
		t.Fatal("expected one build ID but got ", buildIDs)
	}
	expected := "4fcb712aa6387724a9f465a32cd8c14b"
	if hex.EncodeToString(buildIDs[0]) != expected {
		t.Fatal("expected ", expected, " but got ", buildIDs[0])
	}
}

func TestStrippedBuildIDs(t *testing.T) {
	testfile, err := getTestdataPath("libc.elf.stripped")
	if err != nil {
		t.Fatal(err)
	}
	f, err := os.Open(testfile)
	if err != nil {
		t.Fatal("from os.Open: ", err)
	}
	buildIDs, err := GetBuildIDs(testfile, f)
	if err != nil {
		t.Fatal("from GetBuildIDs: ", err)
	}
	if len(buildIDs) != 1 {
		t.Fatal("expected one build ID but got ", buildIDs)
	}
	expected := "4fcb712aa6387724a9f465a32cd8c14b"
	if hex.EncodeToString(buildIDs[0]) != expected {
		t.Fatal("expected ", expected, " but got ", buildIDs[0])
	}
}
