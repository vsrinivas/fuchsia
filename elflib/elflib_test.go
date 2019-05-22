// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package elflib

import (
	"encoding/hex"
	"os"
	"testing"
)

func TestBuildIDs(t *testing.T) {
	f, err := os.Open("testdata/libc.elf.section-only")
	if err != nil {
		t.Fatal("from os.Open: ", err)
	}
	buildIDs, err := GetBuildIDs("testdata/libc.elf.section-only", f)
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
	f, err := os.Open("testdata/libc.elf.stripped")
	if err != nil {
		t.Fatal("from os.Open: ", err)
	}
	buildIDs, err := GetBuildIDs("testdata/libc.elf.stripped", f)
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
