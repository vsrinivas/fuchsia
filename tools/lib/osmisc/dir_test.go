// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package osmisc

import (
	"bytes"
	"io/ioutil"
	"os"
	"path/filepath"
	"testing"
)

func TestDirIsEmpty(t *testing.T) {
	tmpDir := t.TempDir()

	// Directory should start off empty.
	empty, err := DirIsEmpty(tmpDir)
	if err != nil {
		t.Fatal(err.Error())
	} else if !empty {
		t.Fatalf("directory should be empty")
	}

	if err := ioutil.WriteFile(filepath.Join(tmpDir, "file.txt"), []byte("content"), 0o600); err != nil {
		t.Fatal(err.Error())
	}

	// Directory should now be non-empty.
	empty, err = DirIsEmpty(tmpDir)
	if err != nil {
		t.Fatal(err.Error())
	} else if empty {
		t.Fatalf("directory should be non-empty")
	}

	// Non-existent directories should be empty by convention.
	nonexistentSubdir := filepath.Join(tmpDir, "i_dont_exist")
	empty, err = DirIsEmpty(nonexistentSubdir)
	if err != nil {
		t.Fatal(err.Error())
	} else if !empty {
		t.Fatalf("non-existent directory should be empty")
	}

}

func TestCopyDir(t *testing.T) {
	tmpDir := t.TempDir()

	srcDir := filepath.Join(tmpDir, "src")
	if err := os.Mkdir(srcDir, 0o700); err != nil {
		t.Fatalf("failed to create src %q: %v", srcDir, err)
	}

	dstDir := filepath.Join(tmpDir, "dst")
	if err := os.Mkdir(dstDir, 0o700); err != nil {
		t.Fatalf("failed to create dst %q: %v", dstDir, err)
	}

	srcPaths := map[string]string{
		"a":     "a",
		"b":     "",
		"b/a":   "b/a",
		"b/b":   "b/b",
		"b/c":   "",
		"b/c/a": "b/c/a",
	}

	for path, contents := range srcPaths {
		path = filepath.Join(srcDir, path)

		if err := os.MkdirAll(filepath.Dir(path), 0o700); err != nil {
			t.Fatalf("failed to create path %q: %v", path, err)
		}

		if contents == "" {
			if err := os.MkdirAll(path, 0o700); err != nil {
				t.Fatalf("failed to create path %q: %v", path, err)
			}
		} else {
			if err := ioutil.WriteFile(path, []byte(contents), 0o400); err != nil {
				t.Fatalf("failed to write contents to src %q: %v", path, err)
			}
		}
	}

	if err := os.Symlink(filepath.Join(srcDir, "a"), filepath.Join(srcDir, "c")); err != nil {
		t.Fatalf("failed to create symlink: %v", err)
	}
	srcPaths["c"] = "a"

	if err := CopyDir(srcDir, dstDir); err != nil {
		t.Fatalf("failed to copy directory: %v", err)
	}

	err := filepath.Walk(dstDir, func(dstPath string, dstInfo os.FileInfo, err error) error {
		if dstDir == dstPath {
			return nil
		}

		if err != nil {
			return err
		}

		relPath, err := filepath.Rel(dstDir, dstPath)
		if err != nil {
			t.Fatalf("failed to get relative path for dst: %v", err)
		}

		if _, ok := srcPaths[relPath]; !ok {
			t.Fatalf("unknown path %q", relPath)
		}
		delete(srcPaths, relPath)

		srcPath := filepath.Join(srcDir, relPath)
		srcInfo, err := os.Lstat(srcPath)
		if err != nil {
			t.Fatalf("failed to stat src %q: %v", srcPath, err)
		}

		if srcInfo.Mode() != dstInfo.Mode() {
			t.Fatalf("mode not copied from %q to %q: %s != %s", srcPath, dstPath, srcInfo.Mode(), dstInfo.Mode())
		}

		if !srcInfo.IsDir() {
			srcContents, err := ioutil.ReadFile(srcPath)
			if err != nil {
				t.Fatalf("failed to read src %q: %v", srcPath, err)
			}

			dstContents, err := ioutil.ReadFile(dstPath)
			if err != nil {
				t.Fatalf("failed to read dst %q: %v", dstPath, err)
			}

			if !bytes.Equal(srcContents, dstContents) {
				t.Fatalf("src %q has different contents than dst %q", srcPath, dstPath)
			}
		}

		return nil
	})
	if err != nil {
		t.Fatalf("failed to walk directory: %v", err)
	}

	if len(srcPaths) != 0 {
		t.Fatalf("some files not copied: %+v", srcPaths)
	}
}
