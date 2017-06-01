// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"

	"fuchsia.googlesource.com/pm/pkg"
	"golang.org/x/crypto/ed25519"
)

// TestFiles is the list of files created by the default factories in this package.
var TestFiles = []string{"a", "b", "dir/c"}

// TestPackage initializes a set of files into a package directory next to the
// config manifest
func TestPackage(cfg *Config) {
	p := pkg.Package{Name: "testpackage", Version: "0"}

	pkgPath := filepath.Join(filepath.Dir(cfg.ManifestPath), "package")
	if err := os.MkdirAll(filepath.Join(pkgPath, "meta"), os.ModePerm); err != nil {
		panic(err)
	}
	pkgJSON := filepath.Join(pkgPath, "meta", "package.json")
	b, err := json.Marshal(&p)
	if err != nil {
		panic(err)
	}
	if err := ioutil.WriteFile(pkgJSON, b, os.ModePerm); err != nil {
		panic(err)
	}

	_, pkey, err := ed25519.GenerateKey(nil)
	if err != nil {
		panic(err)
	}
	if err := ioutil.WriteFile(cfg.KeyPath, []byte(pkey), os.ModePerm); err != nil {
		panic(err)
	}

	mfst, err := os.Create(cfg.ManifestPath)
	if err != nil {
		panic(err)
	}
	if _, err := fmt.Fprintf(mfst, "meta/package.json=%s\n", pkgJSON); err != nil {
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
		if _, err := fmt.Fprintf(f, "%s\n", name); err != nil {
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
