// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package symbolize

import (
	"bytes"
	"encoding/hex"
	"os"
	"testing"

	"fuchsia.googlesource.com/tools/elflib"
)

func hexEqual(a []byte, b string) bool {
	bBytes, _ := hex.DecodeString(b)
	return bytes.Equal(a, bBytes)
}

func TestGoBug(t *testing.T) {
	data, err := os.Open("testdata/gobug.elf")
	if err != nil {
		t.Fatal(err)
	}
	defer data.Close()
	buildids, err := elflib.GetBuildIDs("gobug.exe", data)
	if err != nil {
		t.Fatal(err)
	}
	if len(buildids) != 3 {
		t.Error("expected", 3, "build ids but got", len(buildids))
		return
	}
	expected := []string{
		"5bf6a28a259b95b4f20ffbcea0cbb149",
		"4FCB712AA6387724A9F465A3DEADBEEF",
		"DEADBEEFA6387724A9F465A32CD8C14B",
	}
	// Test that we get exactly the build ids we expect
	for i, expect := range expected {
		if !hexEqual(buildids[i], expect) {
			t.Error("expected", expect, "got", hex.EncodeToString(buildids[i]))
		}
	}
}
