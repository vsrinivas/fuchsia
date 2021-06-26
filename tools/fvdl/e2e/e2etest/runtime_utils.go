// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package e2etest

import (
	"archive/tar"
	"compress/gzip"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"path/filepath"
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
func ExtractPackage(tgzFile, destRoot string) error {
	fmt.Printf("[info] Extracting fuchsia image file from '%s'", tgzFile)
	f, err := os.Open(tgzFile)
	if err != nil {
		return fmt.Errorf("error opening tgz file: %w", err)
	}
	defer f.Close()

	gr, err := gzip.NewReader(f)
	if err != nil {
		return fmt.Errorf("error unzipping tgz file: %w", err)
	}
	defer gr.Close()

	tr := tar.NewReader(gr)
	for {
		hdr, err := tr.Next()
		if err == io.EOF {
			break
		}
		if err != nil {
			return fmt.Errorf("error reading tgz file: %w", err)
		}
		name := hdr.Name

		switch hdr.Typeflag {
		case tar.TypeReg:
			destFile := filepath.Join(destRoot, name)
			if err := os.MkdirAll(filepath.Dir(destFile), 0o755); err != nil {
				return err
			}

			img, err := os.Create(destFile)
			if err != nil {
				return fmt.Errorf("error creating image file %q: %w", destFile, err)
			}
			if _, err := io.Copy(img, tr); err != nil {
				return fmt.Errorf("error extracting image file %q from tgz file: %w", destFile, err)
			}
			if err := img.Close(); err != nil {
				return fmt.Errorf("error closing image file %q: %w", destFile, err)
			}
		}
	}
	return nil
}

// GenerateFakeArgsFile creates a fake gn build output to destFile.
func GenerateFakeArgsFile(destFile string) error {
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
	return ioutil.WriteFile(destFile, fakeData, 0o755)
}

// GenerateFakeImagesJson creates a fake images.json build output to destFile.
func GenerateFakeImagesJson(destFile string) error {
	// This is a fake images.json file that can be found in build artifact (i.e out/default/images.json).
	// The content is not important, except the "name" and "type" needs to match fvdl's parser logic.
	// In the test, we are overriding the files by setting the args --fvm_image, --kernel_image, etc.
	fakeData := []byte(`[
		{
			"archive": true,
			"label": "//build/images:build_args_metadata(//build/toolchain/fuchsia:x64)",
			"name": "buildargs",
			"path": "args.gn",
			"type": "gn"
		},
		{
			"archive": true,
	  		"cpu": "x64",
			"label": "//zircon/kernel/arch/x86/phys/boot-shim:multiboot-shim(//zircon/kernel/arch/x86/phys:kernel.phys32)",
			"name": "qemu-kernel",
			"path": "dummy_multiboot.bin",
			"type": "kernel"
		},
		{
			"archive": true,
			"bootserver_pave": [
			  "--boot",
			  "--zircona"
			],
			"fastboot_flash": [
			],
			"label": "//build/images:fuchsia(//build/toolchain/fuchsia:x64)",
			"name": "zircon-a",
			"path": "dummy_fuchsia.zbi",
			"type": "zbi"
		}
	]
`)
	return ioutil.WriteFile(destFile, fakeData, 0o755)
}
