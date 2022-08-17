// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func makeTestManifestFile(t *testing.T) (string, map[string]string) {
	p := filepath.Join(t.TempDir(), "manifest")
	wantPaths := map[string]string{
		"a": "/somepath/a",
		"1": "/somepath/b",
	}

	out := ""
	for k, v := range wantPaths {
		out += fmt.Sprintf("%s=%s\n", k, v)
	}
	// Write some junk.
	out += "\na\n"
	if err := os.WriteFile(p, []byte(out), 0o600); err != nil {
		t.Fatal(err)
	}
	return p, wantPaths
}

func validateMapping(t *testing.T, m *Manifest, f map[string]string) {
	if len(m.Paths) != len(f) {
		t.Errorf("lengths differ: got %d, want %d", len(m.Paths), len(f))
	}

	for wantDest, wantSource := range f {
		source, ok := m.Paths[wantDest]
		if !ok {
			t.Errorf("manifest is missing file %q", wantDest)
		}

		if source != wantSource {
			t.Errorf("source: got %q, want %q", source, wantSource)
		}
	}
}

// makeTestFileDir create a directory with files and a directory.
func makeTestFileDir(t *testing.T) (string, []string) {
	d := t.TempDir()
	files := []string{"a", "b", filepath.Join("dir", "c")}
	for _, f := range files {
		path := filepath.Join(d, f)
		if err := os.MkdirAll(filepath.Dir(path), 0o700); err != nil {
			t.Fatal(err)
		}
		f, err := os.Create(path)
		if err != nil {
			t.Fatal(err)
		}
		f.Close()
	}

	return d, files
}

func TestNewManifest_withManifestAndDirectory(t *testing.T) {
	dir, wantFiles := makeTestFileDir(t)
	manifestFile, wantPaths := makeTestManifestFile(t)

	manifest, err := NewManifest([]string{manifestFile, dir})
	if err != nil {
		t.Fatal(err)
	}

	for _, f := range wantFiles {
		wantPaths[f] = filepath.Join(dir, f)
	}

	validateMapping(t, manifest, wantPaths)

}

func TestNewManifest_withDirectory(t *testing.T) {
	d, files := makeTestFileDir(t)

	m, err := NewManifest([]string{d})
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
	f, wantPaths := makeTestManifestFile(t)

	m, err := NewManifest([]string{f})
	if err != nil {
		t.Fatal(err)
	}

	validateMapping(t, m, wantPaths)
}

type manifestEntry struct {
	packagePath string
	filePath    string
	contents    string
}

func makeTestManifest(t *testing.T, entries []manifestEntry) (string, string) {
	tmp := t.TempDir()
	manifestPath := filepath.Join(tmp, "test.manifest")
	manifest, err := os.Create(manifestPath)
	if err != nil {
		t.Fatal(err)
	}
	defer manifest.Close()
	for _, entry := range entries {
		path := filepath.Join(tmp, entry.filePath)
		if err := os.MkdirAll(filepath.Dir(path), 0o700); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(path, []byte(entry.contents), 0o600); err != nil {
			t.Fatal(err)
		}
		if _, err := fmt.Fprintf(manifest, "%s=%s\n", entry.packagePath, path); err != nil {
			t.Fatal(err)
		}
	}
	return tmp, manifestPath
}

func TestNewManifest_withManifest_withDuplicates(t *testing.T) {
	tmp, manifestPath := makeTestManifest(
		t,
		[]manifestEntry{
			{
				packagePath: "bin/app1",
				filePath:    "app1/bin",
				contents:    "app1's unique binary",
			},
			{
				packagePath: "bin/app2",
				filePath:    "app2/bin",
				contents:    "app2's unique binary",
			},
			{
				packagePath: "lib/shared.so",
				filePath:    "app1/lib",
				contents:    "duplicate shared library",
			},
			{
				packagePath: "lib/shared.so",
				filePath:    "app2/lib",
				contents:    "duplicate shared library",
			},
		})
	manifest, err := NewManifest([]string{manifestPath})
	if err != nil {
		t.Fatal(err)
	}
	validateMapping(t, manifest, map[string]string{
		"bin/app1": filepath.Join(tmp, "app1/bin"),
		"bin/app2": filepath.Join(tmp, "app2/bin"),
		// first duplicate entry wins
		"lib/shared.so": filepath.Join(tmp, "app1/lib"),
	})
}

func TestNewManifest_withManifest_withDuplicatesWithUnEqualContent(t *testing.T) {
	_, manifestPath := makeTestManifest(
		t,
		[]manifestEntry{
			{
				packagePath: "lib/shared.so",
				filePath:    "app1/lib",
				contents:    "duplicate shared library",
			},
			{
				packagePath: "lib/shared.so",
				filePath:    "app2/lib",
				contents:    "duplicate shared library with different content",
			},
		})
	if m, err := NewManifest([]string{manifestPath}); err == nil {
		t.Fatalf("should have thrown error, got %v:", m)
	}
}

func TestManifestMeta(t *testing.T) {
	m := &Manifest{
		Paths: map[string]string{
			"meta/package":  "",
			"meta/contents": "",
			"alpha":         "",
			"beta":          "",
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
			"meta/package":  "",
			"meta/contents": "",
			"alpha":         "",
			"beta":          "",
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
