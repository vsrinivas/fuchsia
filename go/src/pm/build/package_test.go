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

	"fuchsia.googlesource.com/pm/merkle"
	"fuchsia.googlesource.com/pm/pkg"

	"golang.org/x/crypto/ed25519"
)

func TestInit(t *testing.T) {
	cfg := TestConfig()
	defer os.RemoveAll(filepath.Dir(cfg.TempDir))

	if err := Init(cfg); err != nil {
		t.Fatal(err)
	}

	f, err := os.Open(filepath.Join(cfg.OutputDir, "meta", "package.json"))
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

	f, err = os.Open(filepath.Join(cfg.OutputDir, "meta", "package.json"))
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

	if len(lines) != len(TestFiles) {
		t.Fatalf("content lines mismatch: %v\n%v", lines, TestFiles)
	}

	files := []string{}
	files = append(files, TestFiles...)
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
func TestSign(t *testing.T) {
	cfg := TestConfig()
	defer os.RemoveAll(filepath.Dir(cfg.TempDir))
	TestPackage(cfg)

	if err := Update(cfg); err != nil {
		t.Fatal(err)
	}

	manifest, err := cfg.Manifest()
	if err != nil {
		t.Fatal(err)
	}

	if _, ok := manifest.Paths["meta/signature"]; ok {
		t.Fatal("unexpected signature file created during test setup")
	}
	if _, ok := manifest.Paths["meta/pubkey"]; ok {
		t.Fatal("unexpected pubkey file created during test setup")
	}

	if err := Sign(cfg); err != nil {
		t.Fatal(err)
	}

	var msg []byte
	for _, f := range manifest.SigningFiles() {
		msg = append(msg, f...)
	}

	for _, f := range manifest.SigningFiles() {
		buf, err := ioutil.ReadFile(manifest.Paths[f])
		if err != nil {
			t.Fatal(err)
		}
		msg = append(msg, buf...)
	}

	f, err := os.Open(manifest.Paths["meta/signature"])
	if err != nil {
		t.Fatal(err)
	}
	sig := make([]byte, 1024)
	n, err := f.Read(sig)
	if err != nil {
		t.Fatal(err)
	}
	sig = sig[:n]

	b, err := ioutil.ReadFile(manifest.Paths["meta/pubkey"])
	if err != nil {
		t.Fatal(err)
	}
	pubkey := ed25519.PublicKey(b)

	if !ed25519.Verify(pubkey, msg, sig) {
		t.Fatal("signature verification failed")
	}
}
func TestVerify(t *testing.T) {
	cfg := TestConfig()
	defer os.RemoveAll(filepath.Dir(cfg.TempDir))
	TestPackage(cfg)

	if err := Update(cfg); err != nil {
		t.Fatal(err)
	}
	if err := Sign(cfg); err != nil {
		t.Fatal(err)
	}

	if err := Verify(cfg); err != nil {
		t.Fatal(err)
	}

	manifest, err := cfg.Manifest()
	if err != nil {
		t.Fatal(err)
	}

	// verification succeeded

	// truncate contents file to invalidate the verification input
	f, err := os.Create(manifest.Paths["meta/contents"])
	if err != nil {
		t.Fatal(err)
	}
	f.Close()

	if err := Verify(cfg); err != ErrVerificationFailed {
		t.Fatalf("got %v, want %v", err, ErrVerificationFailed)
	}

	if err := Update(cfg); err != nil {
		t.Fatal(err)
	}
	if err := Sign(cfg); err != nil {
		t.Fatal(err)
	}

	if err := Verify(cfg); err != nil {
		t.Fatalf("unexpected verification failure: %s", err)
	}

	manifest.Paths["meta/content"] = manifest.Paths["meta/contents"]
	delete(manifest.Paths, "meta/contents")

	if err := Verify(cfg); err != ErrVerificationFailed {
		t.Fatalf("got %v, want %v", err, ErrVerificationFailed)
	}
}

func TestValidate(t *testing.T) {
	cfg := TestConfig()
	defer os.RemoveAll(filepath.Dir(cfg.TempDir))
	TestPackage(cfg)

	if err := Update(cfg); err != nil {
		t.Fatal(err)
	}
	if err := Sign(cfg); err != nil {
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
		if err := Validate(cfg); err == nil {
			t.Errorf("expected a validation error when %q is missing", f)
		}
		manifest.Paths[f] = manifest.Paths[f+"a"]
	}
}

func TestSeal(t *testing.T) {

	cfg := TestConfig()
	defer os.RemoveAll(filepath.Dir(cfg.TempDir))
	TestPackage(cfg)

	if err := Update(cfg); err != nil {
		t.Fatal(err)
	}
	if err := Sign(cfg); err != nil {
		t.Fatal(err)
	}

	if _, err := Seal(cfg); err != nil {
		t.Fatal(err)
	}

	// TODO(raggi): until we have far reader support this test only verifies that
	// the package meta data was correctly updated, it doesn't actually verify
	// that the contents of the far are correct.

	if _, err := os.Stat(filepath.Join(cfg.OutputDir, "meta.far")); err != nil {
		t.Errorf("meta.far was not created")
	}
}
