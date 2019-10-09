// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"crypto/rand"
	"encoding/json"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"testing"

	"fuchsia.googlesource.com/merkle"
	"fuchsia.googlesource.com/pm/pkg"
)

func TestInit(t *testing.T) {
	cfg := TestConfig()
	defer os.RemoveAll(filepath.Dir(cfg.TempDir))

	if err := Init(cfg); err != nil {
		t.Fatal(err)
	}

	f, err := os.Open(filepath.Join(cfg.OutputDir, "meta", "package"))
	if err != nil {
		t.Fatal(err)
	}
	var p pkg.Package
	if err := json.NewDecoder(f).Decode(&p); err != nil {
		t.Fatal(err)
	}
	f.Close()

	if want := cfg.PkgName; p.Name != want {
		t.Errorf("got %q, want %q", p.Name, want)
	}

	if want := "0"; p.Version != want {
		t.Errorf("got %q, want %q", p.Version, want)
	}
	os.RemoveAll(filepath.Dir(cfg.TempDir))

	// test condition where package name is empty, name should match the
	// name of the output directory
	cfg.PkgName = ""
	if err = Init(cfg); err != nil {
		t.Fatal(err)
	}

	f, err = os.Open(filepath.Join(cfg.OutputDir, "meta", "package"))
	if err != nil {
		t.Fatal(err)
	}

	if err = json.NewDecoder(f).Decode(&p); err != nil {
		t.Fatal(err)
	}
	f.Close()

	if want := filepath.Base(cfg.OutputDir); p.Name != want {
		t.Errorf("got %q, want %q", p.Name, want)
	}
}

func TestUpdate(t *testing.T) {
	cfg := TestConfig()
	defer os.RemoveAll(filepath.Dir(cfg.TempDir))
	TestPackage(cfg)

	manifest, err := cfg.Manifest()
	if err != nil {
		t.Fatal(err)
	}

	for _, f := range manifest.Content() {
		fd, err := os.Create(f)
		if err != nil {
			t.Fatal(err)
		}
		if _, err := io.CopyN(fd, rand.Reader, 1024); err != nil {
			t.Fatal(err)
		}
		fd.Close()
	}

	if _, ok := manifest.Meta()["meta/contents"]; ok {
		t.Fatalf("unexpected pre-existing meta/contents in manifest")
	}

	if err := Update(cfg); err != nil {
		t.Fatal(err)
	}

	contentsPath, ok := manifest.Meta()["meta/contents"]
	if !ok {
		t.Fatalf("meta/contents was not found in manifest after update")
	}

	f, err := os.Open(contentsPath)
	if err != nil {
		t.Fatal(err)
	}
	buf, err := ioutil.ReadAll(f)
	f.Close()
	if err != nil {
		t.Fatal(err)
	}

	lines := strings.Split(string(buf), "\n")
	sort.Strings(lines)
	if lines[0] == "" {
		lines = lines[1:]
	}

	contentFiles := []string{}
	for _, s := range TestFiles {
		if strings.HasPrefix(s, "meta/") {
			continue
		}
		contentFiles = append(contentFiles, s)
	}
	if len(lines) != len(contentFiles) {
		t.Fatalf("content lines mismatch: %v\n%v", lines, contentFiles)
	}

	files := []string{}
	files = append(files, contentFiles...)
	sort.Strings(files)
	for i, pf := range files {
		var tree merkle.Tree
		f, err := os.Open(manifest.Paths[pf])
		if err != nil {
			t.Fatal(err)
		}
		if _, err := tree.ReadFrom(f); err != nil {
			t.Fatal(err)
		}
		f.Close()
		want := fmt.Sprintf("%s=%x", pf, tree.Root())
		if lines[i] != want {
			t.Errorf("contents mismatch: got %q, want %q", lines[i], want)
			break
		}
	}
}

func TestValidate(t *testing.T) {
	cfg := TestConfig()
	defer os.RemoveAll(filepath.Dir(cfg.TempDir))
	TestPackage(cfg)

	if err := Update(cfg); err != nil {
		t.Fatal(err)
	}

	err := Validate(cfg)
	if err != nil {
		t.Fatalf("Unexpected validation error")
	}

	manifest, err := cfg.Manifest()
	if err != nil {
		t.Fatal(err)
	}

	for _, f := range RequiredFiles {
		manifest.Paths[f+"a"] = manifest.Paths[f]
		delete(manifest.Paths, f)
		if err := Validate(cfg); err == nil {
			t.Errorf("expected a validation error when %q is missing", f)
		}
		manifest.Paths[f] = manifest.Paths[f+"a"]
		delete(manifest.Paths, f+"a")
	}
}

func TestSeal(t *testing.T) {
	cfg := TestConfig()
	defer os.RemoveAll(filepath.Dir(cfg.TempDir))
	TestPackage(cfg)

	if err := Update(cfg); err != nil {
		t.Fatal(err)
	}

	if _, err := Seal(cfg); err != nil {
		t.Fatal(err)
	}

	// TODO(raggi): until we have far reader support this test only verifies that
	// the package meta data was correctly updated, it doesn't actually verify
	// that the contents of the far are correct.
	metafar := filepath.Join(cfg.OutputDir, "meta.far")
	if _, err := os.Stat(metafar); err != nil {
		t.Errorf("meta.far was not created")
	}

	merklefile := filepath.Join(cfg.OutputDir, "meta.far.merkle")
	if _, err := os.Stat(merklefile); err != nil {
		t.Errorf("meta.far.merkle was not created")
	}

	m, err := ioutil.ReadFile(merklefile)
	if err != nil {
		t.Fatal(err)
	}
	fromFile := string(m)

	var tree merkle.Tree
	f, err := os.Open(metafar)
	if err != nil {
		t.Fatal(err)
	}
	tree.ReadFrom(f)
	fromFar := fmt.Sprintf("%x", tree.Root())

	if fromFile != fromFar {
		t.Errorf("meta.far.merkle != merkle(meta.far)\n%q\n%q", fromFile, fromFar)
	}
}
