// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package elflib

import (
	"path/filepath"
	"sort"
	"testing"
)

const (
	// The associated cycle.elf was copied from the upstream debug/dwarf
	// package test data.
	cycleSource = "/home/austin/go.dev/src/debug/dwarf/testdata/cycle.c"
)

func TestListSources(t *testing.T) {
	file := filepath.Join(*testDataFlag, "cycle.elf")
	srcs, err := ListSources(file)
	if err != nil {
		t.Fatalf("failed to read sources from %q: %v", file, err)
	}
	sort.Strings(srcs)

	if len(srcs) != 1 {
		t.Fatalf("unexpected number of sources: %v", srcs)
	}
	if srcs[0] != cycleSource {
		t.Fatalf("unexpected source: %v", srcs[0])
	}
}
