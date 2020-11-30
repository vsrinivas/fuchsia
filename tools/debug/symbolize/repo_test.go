// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package symbolize

import (
	"bytes"
	"encoding/hex"
	"os"
	"path/filepath"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/debug/elflib"
)

func hexEqual(a []byte, b string) bool {
	bBytes, _ := hex.DecodeString(b)
	return bytes.Equal(a, bBytes)
}

type gobugMockSource struct{}

func (g *gobugMockSource) GetBinaries() ([]elflib.BinaryFileRef, error) {
	p := filepath.Join(*testDataDir, "gobug.elf")
	out := []elflib.BinaryFileRef{
		{BuildID: "5bf6a28a259b95b4f20ffbcea0cbb149", Filepath: p},
		{BuildID: "4FCB712AA6387724A9F465A3DEADBEEF", Filepath: p},
		{BuildID: "DEADBEEFA6387724A9F465A32CD8C14B", Filepath: p},
	}
	for _, bin := range out {
		if err := bin.Verify(); err != nil {
			return nil, err
		}
	}
	return out, nil
}

// The current go toolchain used in Fuchsia has a bug that causes multiple build ids
// to wind up in go binaries. This tests to make sure we can handle that case and
// still ensure that our mentioned build id matches any of the build ids in the file.
func TestGoBug(t *testing.T) {
	source := &gobugMockSource{}
	p := filepath.Join(*testDataDir, "gobug.elf")
	data, err := os.Open(p)
	if err != nil {
		t.Fatal(err)
	}
	defer data.Close()
	buildids, err := elflib.GetBuildIDs(p, data)
	if err != nil {
		t.Fatal(err)
	}
	if len(buildids) != 3 {
		t.Error("expected", 3, "build IDs but got", len(buildids))
		return
	}
	// Test that we get exactly the build IDs we expect
	bins, err := source.GetBinaries()
	if err != nil {
		t.Fatal(err)
	}
	for i, bin := range bins {
		if !hexEqual(buildids[i], bin.BuildID) {
			t.Error("expected", bin.BuildID, "got", hex.EncodeToString(buildids[i]))
		}
	}
}
