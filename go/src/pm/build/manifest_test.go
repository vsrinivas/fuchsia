// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"testing"
)

func TestNewManifest_withDirectory(t *testing.T) {
	d, err := ioutil.TempDir("", t.Name())
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(d)

	files := []string{"a", "b", "dir/c"}
	for _, f := range files {
		path := filepath.Join(d, f)
		os.MkdirAll(filepath.Dir(path), os.ModePerm)
		f, err := os.Create(path)
		if err != nil {
			t.Fatal(err)
		}
		f.Close()
	}

	m, err := NewManifest(d)
	if err != nil {
		t.Fatal(err)
	}

	if len(m.Paths) != len(files) {
		t.Errorf("lengths differ: %v vs %v", m.Paths, files)
	}

	for _, f := range files {
		source, ok := m.Paths[f]
		if !ok {
			t.Errorf("manifest is missing file %q", f)
			continue
		}
		if want := filepath.Join(d, f); source != want {
			t.Errorf("source: got %q, want %q", source, want)
		}
	}

}

func TestNewManifest_withManifest(t *testing.T) {
	f, err := ioutil.TempFile("", t.Name())
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(f.Name())

	wantPaths := map[string]string{
		"a": "/somepath/a",
		"b": "/somepath/b",
	}

	for k, v := range wantPaths {
		_, err := fmt.Fprintf(f, "%s=%s\n", k, v)
		if err != nil {
			t.Fatal(err)
		}
	}
	// write some junk
	fmt.Fprint(f, "\na\n")
	f.Close()

	m, err := NewManifest(f.Name())
	if err != nil {
		t.Fatal(err)
	}

	if len(wantPaths) != len(m.Paths) {
		t.Errorf("incorrect length from parsed manifest")
	}

	for wantDest, wantSource := range wantPaths {
		source, ok := m.Paths[wantDest]
		if !ok {
			t.Errorf("manifest is missing file %q", wantDest)
		}

		if source != wantSource {
			t.Errorf("source: got %q, want %q", source, wantSource)
		}
	}
}

func TestManifestMeta(t *testing.T) {
	m := &Manifest{
		Paths: map[string]string{
			"meta/package.json": "",
			"meta/contents":     "",
			"alpha":             "",
			"beta":              "",
		},
	}

	if got, want := len(m.Meta()), 2; got != want {
		t.Errorf("got %d, want %d", got, want)
	}

	for k := range m.Meta() {
		if !strings.HasPrefix(k, "meta/") {
			t.Errorf("found non-meta file in metas: %q", k)
		}
	}
}
func TestManifestContent(t *testing.T) {
	m := &Manifest{
		Paths: map[string]string{
			"meta/package.json": "",
			"meta/contents":     "",
			"alpha":             "",
			"beta":              "",
		},
	}

	if got, want := len(m.Meta()), 2; got != want {
		t.Errorf("got %d, want %d", got, want)
	}

	for k := range m.Content() {
		if strings.HasPrefix(k, "meta/") {
			t.Errorf("found meta file in contents: %q", k)
		}
	}
}

func TestManifestSigningfiles(t *testing.T) {
	m := &Manifest{
		Paths: map[string]string{
			"meta/package.json": "",
			"meta/contents":     "",
			"meta/signature":    "",
			"alpha":             "",
			"beta":              "",
		},
	}
	want := []string{"meta/package.json", "meta/contents"}
	sort.Strings(want)

	got := m.SigningFiles()

	if len(got) != len(want) {
		t.Fatalf("got %v, want %v", got, want)
	}

	for i, w := range want {
		g := got[i]
		if g != w {
			t.Errorf("got %q, want %q", g, w)
		}
	}
}
