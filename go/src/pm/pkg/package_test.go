// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pkg

import (
	"bytes"
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

	"golang.org/x/crypto/ed25519"

	"fuchsia.googlesource.com/pm/keys"
	"fuchsia.googlesource.com/pm/merkle"
	"fuchsia.googlesource.com/pm/testpackage"
)

func TestInit(t *testing.T) {
	tmpDir, err := testpackage.New()
	defer os.RemoveAll(tmpDir)
	if err != nil {
		t.Fatal(err)
	}

	if err := Init(tmpDir); err != nil {
		t.Fatal(err)
	}

	f, err := os.Open(filepath.Join(tmpDir, "meta", "package.json"))
	if err != nil {
		t.Fatal(err)
	}
	var p Package
	if err := json.NewDecoder(f).Decode(&p); err != nil {
		t.Fatal(err)
	}
	f.Close()

	if want := filepath.Base(tmpDir); p.Name != want {
		t.Errorf("got %q, want %q", p.Name, want)
	}

	if want := "0"; p.Version != want {
		t.Errorf("got %q, want %q", p.Version, want)
	}

	f, err = os.Open(filepath.Join(tmpDir, "meta", "contents"))
	if err != nil {
		t.Fatal(err)
	}
	buf, err := ioutil.ReadAll(f)
	if err != nil {
		t.Fatal(err)
	}

	lines := strings.Split(string(buf), "\n")
	sort.Strings(lines)
	if lines[0] == "" {
		lines = lines[1:]
	}

	for i, pf := range testpackage.Files {
		if lines[i] != pf {
			t.Errorf("contents mismatch: got %q, want %q", lines[i], pf)
			break
		}
	}
}

func TestUpdate(t *testing.T) {
	tmpDir, err := testpackage.New()
	defer os.RemoveAll(tmpDir)
	if err != nil {
		t.Fatal(err)
	}

	for _, f := range testpackage.Files {
		fd, err := os.Create(filepath.Join(tmpDir, f))
		if err != nil {
			t.Fatal(err)
		}
		if _, err := io.CopyN(fd, rand.Reader, 1024); err != nil {
			t.Fatal(err)
		}
		fd.Close()
	}

	if err := Init(tmpDir); err != nil {
		t.Fatal(err)
	}

	if err := Update(tmpDir); err != nil {
		t.Fatal(err)
	}

	f, err := os.Open(filepath.Join(tmpDir, "meta", "contents"))
	if err != nil {
		t.Fatal(err)
	}
	buf, err := ioutil.ReadAll(f)
	if err != nil {
		t.Fatal(err)
	}

	lines := strings.Split(string(buf), "\n")
	sort.Strings(lines)
	if lines[0] == "" {
		lines = lines[1:]
	}

	// meta/package.json is also included
	if len(lines) != len(testpackage.Files)+1 {
		t.Fatalf("content lines mismatch: %v\n%v", lines, testpackage.Files)
	}

	files := []string{}
	files = append(files, testpackage.Files...)
	files = append(files, "meta/package.json")
	for i, pf := range files {
		var tree merkle.Tree
		f, err := os.Open(filepath.Join(tmpDir, pf))
		if err != nil {
			t.Fatal(err)
		}
		if _, err := tree.ReadFrom(f); err != nil {
			t.Fatal(err)
		}
		f.Close()
		want := fmt.Sprintf("%s:%x", pf, tree.Root())
		if lines[i] != want {
			t.Errorf("contents mismatch: got %q, want %q", lines[i], want)
			break
		}
	}
}
func TestSign(t *testing.T) {
	d, err := testpackage.New()
	defer os.RemoveAll(d)
	if err != nil {
		t.Fatal(err)
	}

	buf := bytes.NewBuffer(nil)
	if err := keys.Gen(buf); err != nil {
		t.Fatal(err)
	}
	key := ed25519.PrivateKey(buf.Bytes())

	if err := Init(d); err != nil {
		t.Fatal(err)
	}
	if err := Update(d); err != nil {
		t.Fatal(err)
	}

	if _, err := os.Stat(filepath.Join(d, "meta", "signature")); !os.IsNotExist(err) {
		t.Fatal("unexpected signature file created during test setup")
	}
	if _, err := os.Stat(filepath.Join(d, "meta", "pubkey")); !os.IsNotExist(err) {
		t.Fatal("unexpected pubkey file created during test setup")
	}

	if err := Sign(d, key); err != nil {
		t.Fatal(err)
	}

	var msg []byte
	metaFiles, err := filepath.Glob(filepath.Join(d, "meta", "*"))
	if err != nil {
		t.Fatal(err)
	}
	sort.Strings(metaFiles)
	for _, f := range metaFiles {
		switch filepath.Base(f) {
		case "signature":
			continue
		}
		msg = append(msg, f...)
	}

	for _, f := range metaFiles {
		switch filepath.Base(f) {
		case "signature":
			continue
		}

		buf, err := ioutil.ReadFile(f)
		if err != nil {
			t.Fatal(err)
		}
		msg = append(msg, buf...)
	}

	f, err := os.Open(filepath.Join(d, "meta", "signature"))
	if err != nil {
		t.Fatal(err)
	}
	sig := make([]byte, 1024)
	n, err := f.Read(sig)
	if err != nil {
		t.Fatal(err)
	}
	sig = sig[:n]

	b, err := ioutil.ReadFile(filepath.Join(d, "meta", "pubkey"))
	if err != nil {
		t.Fatal(err)
	}
	pubkey := ed25519.PublicKey(b)

	if !ed25519.Verify(pubkey, msg, sig) {
		t.Fatal("signature verification failed")
	}
}
func TestVerify(t *testing.T) {
	d, err := testpackage.New()
	defer os.RemoveAll(d)
	if err != nil {
		t.Fatal(err)
	}

	buf := bytes.NewBuffer(nil)
	if err := keys.Gen(buf); err != nil {
		t.Fatal(err)
	}
	key := ed25519.PrivateKey(buf.Bytes())

	if err := Init(d); err != nil {
		t.Fatal(err)
	}
	if err := Update(d); err != nil {
		t.Fatal(err)
	}
	if err := Sign(d, key); err != nil {
		t.Fatal(err)
	}

	if err := Verify(d); err != nil {
		t.Fatal(err)
	}

	// verification succeeded

	// truncate contents file to invalidate the verification input
	f, err := os.Create(filepath.Join(d, "meta", "contents"))
	if err != nil {
		t.Fatal(err)
	}
	f.Close()

	if err := Verify(d); err != ErrVerificationFailed {
		t.Fatalf("got %v, want %v", err, ErrVerificationFailed)
	}

	if err := Update(d); err != nil {
		t.Fatal(err)
	}
	if err := Sign(d, key); err != nil {
		t.Fatal(err)
	}

	if err := Verify(d); err != nil {
		t.Fatalf("unexpected verification failure: %s", err)
	}

	if err := os.Rename(filepath.Join(d, "meta", "contents"), filepath.Join(d, "meta", "content")); err != nil {
		t.Fatal(err)
	}

	if err := Verify(d); err != ErrVerificationFailed {
		t.Fatalf("got %v, want %v", err, ErrVerificationFailed)
	}
}

func TestValidate(t *testing.T) {
	d, err := testpackage.New()
	defer os.RemoveAll(d)
	if err != nil {
		t.Fatal(err)
	}

	if err := Init(d); err != nil {
		t.Fatal(err)
	}
	if err := Update(d); err != nil {
		t.Fatal(err)
	}
	if err := keys.Gen(d); err != nil {
		t.Fatal(err)
	}
	buf, err := ioutil.ReadFile(filepath.Join(d, "key"))
	if err != nil {
		t.Fatal(err)
	}
	key := ed25519.PrivateKey(buf)
	if err := Sign(d, key); err != nil {
		t.Fatal(err)
	}

	err = Validate(d)
	if err != nil {
		t.Fatalf("Unexpected validation error")
	}

	for _, f := range RequiredFiles {
		if err := os.Rename(filepath.Join(d, f), filepath.Join(d, f+"a")); err != nil {
			t.Fatal(err)
		}
		if err := Validate(d); err == nil {
			t.Errorf("expected a validation error when %q is missing", f)
		}
		if err := os.Rename(filepath.Join(d, f+"a"), filepath.Join(d, f)); err != nil {
			t.Fatal(err)
		}
	}
}

func TestSeal(t *testing.T) {
	d, err := testpackage.New()
	defer os.RemoveAll(d)
	if err != nil {
		t.Fatal(err)
	}

	if err := Init(d); err != nil {
		t.Fatal(err)
	}
	if err := Update(d); err != nil {
		t.Fatal(err)
	}
	if err := keys.Gen(d); err != nil {
		t.Fatal(err)
	}
	buf, err := ioutil.ReadFile(filepath.Join(d, "key"))
	if err != nil {
		t.Fatal(err)
	}
	key := ed25519.PrivateKey(buf)
	if err := Sign(d, key); err != nil {
		t.Fatal(err)
	}

	if err := Seal(d); err != nil {
		t.Fatal(err)
	}

	// TODO(raggi): until we have far reader support this test only verifies that
	// the package meta data was correctly updated, it doesn't actually verify
	// that the contents of the far are correct.

	if _, err := os.Stat(filepath.Join(d, "meta.far")); err != nil {
		t.Errorf("meta.far was not created")
	}
}

func TestWalkContents(t *testing.T) {
	d, err := testpackage.New()
	defer os.RemoveAll(d)
	if err != nil {
		t.Fatal(err)
	}

	ignoredFiles := []string{
		"meta/signature",
		"meta/contents",
		".git/HEAD",
		".jiri/manifest",
	}

	for _, f := range ignoredFiles {
		touch(filepath.Join(d, f))
	}
	touch(filepath.Join(d, "meta/package.json"))

	found := map[string]struct{}{}
	err = WalkContents(d, func(path string) error {
		found[path] = struct{}{}
		return nil
	})
	if err != nil {
		t.Fatal(err)
	}

	if len(found) != 4 {
		t.Errorf("unexpected number of files walked. Found: %#v", found)
	}

	for _, f := range testpackage.Files {
		if _, ok := found[f]; !ok {
			t.Errorf("package file %q was not found", f)
		}
	}
	if _, ok := found["meta/package.json"]; !ok {
		t.Errorf("expected to find %s", "meta/package.json")
	}

	for _, f := range ignoredFiles {
		if _, ok := found[f]; ok {
			t.Errorf("walk contents did not ignore file %q", f)
		}
	}
}

// touch creates a file at the given path, it panics on error
func touch(path string) {
	if err := os.MkdirAll(filepath.Dir(path), os.ModePerm); err != nil {
		panic(err)
	}
	f, err := os.Create(path)
	if err != nil {
		panic(err)
	}
	if err := f.Close(); err != nil {
		panic(err)
	}
}
