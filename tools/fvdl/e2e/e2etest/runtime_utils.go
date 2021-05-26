// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package e2etest

import (
	"archive/tar"
	"compress/gzip"
	"io"
	"io/ioutil"
	"os"
	"path/filepath"
	"testing"
)

// FindFileFromDir searches the root dir and returns the first path that matches fileName.
func FindFileFromDir(root, fileName string) string {
	filePath := ""
	filepath.WalkDir(root,
		func(path string, info os.DirEntry, err error) error {
			if err != nil {
				return err
			}
			if !info.IsDir() && info.Name() == fileName {
				filePath = filepath.Dir(path)
			}
			return nil
		})
	return filePath
}

// FindDirFromDir searches the root dir and returns the first path that matches dirName.
func FindDirFromDir(root, dirName string) string {
	filePath := ""
	filepath.WalkDir(root,
		func(path string, info os.DirEntry, err error) error {
			if err != nil {
				return err
			}
			if info.IsDir() && info.Name() == dirName {
				filePath = path
			}
			return nil
		})
	return filePath
}

// ExtractPackage extracts a tgzFile to destRoot.
// For example, ExtractPackage("/root/path/to/file.tar.gz", "/new/path")
// will extract /root/path/to/file.tar.gz to /new/path/...
func ExtractPackage(t *testing.T, tgzFile, destRoot string) {
	t.Logf("[info] Extracting fuchsia image file from '%s'", tgzFile)

	f, err := os.Open(tgzFile)
	if err != nil {
		t.Fatalf("error opening tgz file: %v", err)
	}
	defer f.Close()

	gr, err := gzip.NewReader(f)
	if err != nil {
		t.Fatalf("error unzipping tgz file: %v", err)
	}
	defer gr.Close()

	tr := tar.NewReader(gr)
	for {
		hdr, err := tr.Next()
		if err == io.EOF {
			break
		}
		if err != nil {
			t.Fatalf("error reading tgz file: %v", err)
		}
		name := hdr.Name

		switch hdr.Typeflag {
		case tar.TypeReg:
			destFile := filepath.Join(destRoot, name)
			if err := os.MkdirAll(filepath.Dir(destFile), 0o755); err != nil {
				t.Fatal(err)
			}

			img, err := os.Create(destFile)
			if err != nil {
				t.Fatalf("error creating image file %q: %s", destFile, err)
			}
			if _, err := io.Copy(img, tr); err != nil {
				t.Fatalf("error extracting image file %q from tgz file: %s", destFile, err)
			}
			if err := img.Close(); err != nil {
				t.Fatalf("error closing image file %q: %s", destFile, err)
			}
		}
	}
}

// GenerateFakeArgsFile creates a fake gn build output to destFile.
func GenerateFakeArgsFile(t *testing.T, destFile string) {
	// This is a fake build_args.gni file that can be found in build artifact (i.e out/default/args.gn).
	// VDL parses this file to get product and board information. The content is not important, we only need the file
	// to exist, and contains the lines:
	// ```
	// import("//products/fvdl_e2e_test.gni")
	// import("//vendor/google/boards/qemu-x64.gni")
	// ```
	fakeData := []byte(`import("//products/fvdl_e2e_test.gni")
import("//vendor/google/boards/qemu-x64.gni")
`)
	if err := ioutil.WriteFile(destFile, fakeData, 0o755); err != nil {
		t.Fatal(err)
	}
}
