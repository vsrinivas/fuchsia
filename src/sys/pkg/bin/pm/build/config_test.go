// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"reflect"
	"testing"

	versionHistory "go.fuchsia.dev/fuchsia/src/sys/pkg/lib/version-history"
)

func TestOrderedBlobInfo(t *testing.T) {
	cfg := TestConfig()
	defer os.RemoveAll(filepath.Dir(cfg.TempDir))
	BuildTestPackage(cfg)

	blobs, err := cfg.BlobInfo()
	if err != nil {
		t.Fatal(err)
	}

	// The meta far is always first, and all following content entries are
	// sorted by the path in the package.
	expectedPaths := []string{"meta/", "a", "b", "dir/c", "rand1", "rand2"}

	actualPaths := make([]string, 0, len(blobs))
	for _, blob := range blobs {
		actualPaths = append(actualPaths, blob.Path)
	}

	if !reflect.DeepEqual(actualPaths, expectedPaths) {
		t.Errorf("got %v, want %v", actualPaths, expectedPaths)
	}
}

func TestCannotParseAPILevelAndABIRevision(t *testing.T) {
	cfg := TestConfig()
	defer os.RemoveAll(filepath.Dir(cfg.TempDir))

	fs := flag.NewFlagSet("test", flag.ContinueOnError)
	cfg.InitFlags(fs)

	if err := fs.Parse([]string{"--api-level", "1", "--abi-revision", fmt.Sprintf("%d", testABIRevision)}); err == nil {
		t.Fatalf("expected an error, but parsed ABI revision %x", cfg.PkgABIRevision)
	}
}

func TestParseAPILevelIntoABIRevision(t *testing.T) {
	cfg := TestConfig()
	defer os.RemoveAll(filepath.Dir(cfg.TempDir))

	fs := flag.NewFlagSet("test", flag.ContinueOnError)
	cfg.InitFlags(fs)

	if err := fs.Parse([]string{"--api-level", "1"}); err != nil {
		t.Fatal(err)
	}
	if cfg.PkgABIRevision != testABIRevision {
		t.Fatalf("expected ABI revision %x, not %x", testABIRevision, cfg.PkgABIRevision)
	}
}

func TestParseABIRevisionAsDecimal(t *testing.T) {
	cfg := TestConfig()
	defer os.RemoveAll(filepath.Dir(cfg.TempDir))

	fs := flag.NewFlagSet("test", flag.ContinueOnError)
	cfg.InitFlags(fs)

	if err := fs.Parse([]string{"--abi-revision", fmt.Sprintf("%d", testABIRevision)}); err != nil {
		t.Fatal(err)
	}
	if cfg.PkgABIRevision != testABIRevision {
		t.Fatalf("expected ABI revision %x, not %x", testABIRevision, cfg.PkgABIRevision)
	}
}

func TestParseABIRevisionAsHex(t *testing.T) {
	cfg := TestConfig()
	defer os.RemoveAll(filepath.Dir(cfg.TempDir))

	fs := flag.NewFlagSet("test", flag.ContinueOnError)
	cfg.InitFlags(fs)

	if err := fs.Parse([]string{"--abi-revision", fmt.Sprintf("0x%x", testABIRevision)}); err != nil {
		t.Fatal(err)
	}
	if cfg.PkgABIRevision != testABIRevision {
		t.Fatalf("expected ABI revision %x, not %x", testABIRevision, cfg.PkgABIRevision)
	}
}

func TestParseUnknownABIRevision(t *testing.T) {
	cfg := TestConfig()
	defer os.RemoveAll(filepath.Dir(cfg.TempDir))

	fs := flag.NewFlagSet("test", flag.ContinueOnError)
	cfg.InitFlags(fs)

	// Find an unknown ABI revision by first making a set of all the known
	// versions, and picking one that's not in the set.
	abiRevisions := make(map[uint64]struct{})
	for _, version := range versionHistory.Versions() {
		abiRevisions[version.ABIRevision] = struct{}{}
	}
	var abiRevision uint64 = 1
	for {
		if _, ok := abiRevisions[abiRevision]; !ok {
			break
		}
		abiRevision += 1
	}

	if err := fs.Parse([]string{"--abi-revision", fmt.Sprintf("%d", abiRevision)}); err == nil {
		t.Fatalf("expected an error, but parsed ABI revision %x", cfg.PkgABIRevision)
	}
}
