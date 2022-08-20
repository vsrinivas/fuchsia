// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"crypto/rand"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"path"
	"path/filepath"
	"strings"
)

// TestFiles is the list of files created by the default factories in this package.
var TestFiles = []string{"a", "b", "dir/c", "rand1", "rand2", "meta/test/t"}

// API Level 6
const TestABIRevision uint64 = 0xE9CACD17EA11859D

// API Level 7
const TestABIRevision2 uint64 = 0xECCEA2F70ACD6FC0

// TestPackage initializes a set of files into a package directory next to the
// config manifest
func TestPackage(cfg *Config) {
	p, err := cfg.Package()
	if err != nil {
		panic(err)
	}

	pkgPath := filepath.Join(filepath.Dir(cfg.ManifestPath), "package")
	if err := os.MkdirAll(filepath.Join(pkgPath, "meta"), os.ModePerm); err != nil {
		panic(err)
	}
	pkgJSON := filepath.Join(pkgPath, "meta", "package")
	b, err := json.Marshal(&p)
	if err != nil {
		panic(err)
	}
	if err := os.WriteFile(pkgJSON, b, os.ModePerm); err != nil {
		panic(err)
	}

	mfst, err := os.Create(cfg.ManifestPath)
	if err != nil {
		panic(err)
	}
	if _, err := fmt.Fprintf(mfst, "meta/package=%s\n", pkgJSON); err != nil {
		panic(err)
	}
	// Add additional test directory and file under meta/ for pkgfs meta/ tests.
	if _, err := fmt.Fprintf(mfst, "meta/foo/one=%s\n", pkgJSON); err != nil {
		panic(err)
	}

	for _, name := range TestFiles {
		path := filepath.Join(pkgPath, name)

		err = os.MkdirAll(filepath.Dir(path), os.ModePerm)
		if err != nil {
			panic(err)
		}
		f, err := os.Create(path)
		if err != nil {
			panic(err)
		}
		if strings.HasPrefix(name, "rand") {
			_, err = io.Copy(f, io.LimitReader(rand.Reader, 100))
		} else {
			_, err = fmt.Fprintf(f, "%s\n", name)
		}
		if err != nil {
			panic(err)
		}
		err = f.Close()
		if err != nil {
			panic(err)
		}
		if _, err := fmt.Fprintf(mfst, "%s=%s\n", name, path); err != nil {
			panic(err)
		}
	}

	if err := mfst.Close(); err != nil {
		panic(err)
	}
}

func BuildTestPackage(cfg *Config) {
	TestPackage(cfg)

	if err := Update(cfg); err != nil {
		panic(err)
	}
	if _, err := Seal(cfg); err != nil {
		panic(err)
	}

	outputManifest, err := cfg.OutputManifest()
	if err != nil {
		panic(err)
	}

	outputManifestPath := path.Join(cfg.OutputDir, "package_manifest.json")

	content, err := json.Marshal(outputManifest)
	if err != nil {
		panic(err)
	}
	if err := os.WriteFile(outputManifestPath, content, os.ModePerm); err != nil {
		panic(err)
	}
}

func addTestABIRevisionToManifest(cfg *Config, abiRevision uint64) {
	abiDir := filepath.Join(filepath.Dir(cfg.ManifestPath), "package", "meta", "fuchsia.abi")
	if err := os.MkdirAll(abiDir, os.ModePerm); err != nil {
		panic(err)
	}

	abiPath := filepath.Join(abiDir, "abi-revision")
	b := make([]byte, 8)
	binary.LittleEndian.PutUint64(b, abiRevision)

	// Add the ABI revision to the package manifest.
	f, err := os.OpenFile(cfg.ManifestPath, os.O_RDWR, 0755)
	if err != nil {
		panic(err)
	}
	if _, err := f.Seek(0, 2); err != nil {
		panic(err)
	}
	if _, err := fmt.Fprintf(f, "%s=%s\n", abiRevisionKey, abiPath); err != nil {
		panic(err)
	}

	if err := f.Close(); err != nil {
		panic(err)
	}

	if err := os.WriteFile(abiPath, b, os.ModePerm); err != nil {
		panic(err)
	}
}
