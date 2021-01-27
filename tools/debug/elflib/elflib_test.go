// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package elflib

import (
	"bytes"
	"encoding/hex"
	"flag"
	"os"
	"path/filepath"
	"testing"
)

var testDataDir = flag.String("test_data_dir", "testdata", "Path to testdata/; only used in GN build")

func TestBuildIDs(t *testing.T) {
	testfile := filepath.Join(*testDataDir, "libc.elf.section-only")
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
	testfile := filepath.Join(*testDataDir, "libc.elf.stripped")
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

func TestHasMagic(t *testing.T) {
	{
		buff := []byte("\177ELF...")
		r := bytes.NewReader(buff)
		if !hasElfMagic(r) {
			t.Errorf("expected %q to have ELF magic", buff)
		}
	}

	{
		buff := []byte("Not at the beginning: \177ELF...")
		r := bytes.NewReader(buff)
		if hasElfMagic(r) {
			t.Errorf("expected %q to not have ELF magic", buff)
		}
	}

	{
		buff := []byte("MZ...")
		r := bytes.NewReader(buff)
		if !hasPeMagic(r) {
			t.Errorf("expected %q to have PE magic", buff)
		}
	}

	{
		buff := []byte("Not at the beginning: MZ...")
		r := bytes.NewReader(buff)
		if hasPeMagic(r) {
			t.Errorf("expected %q to not have ELF magic", buff)
		}
	}

	{
		buff := []byte("garbage")
		r := bytes.NewReader(buff)
		if hasElfMagic(r) {
			t.Errorf("expected %q to not have ELF magic", buff)
		}
		if hasPeMagic(r) {
			t.Errorf("expected %q to not have PE magic", buff)
		}
	}
}
