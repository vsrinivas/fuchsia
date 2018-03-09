// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package install implements the `pm install` command
package install

import (
	"bytes"
	"flag"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"

	"fuchsia.googlesource.com/far"
	"fuchsia.googlesource.com/merkle"
	"fuchsia.googlesource.com/pm/build"
)

// Run reads an archive given in flags.Arg(1) (the first argument after
// `install`) and unpacks the archive, adding it's contents to the appropriate
// locations in /pkgfs to install the package.
func Run(cfg *build.Config) error {
	af, err := os.Open(flag.Arg(1))
	if err != nil {
		return err
	}
	defer af.Close()

	pkgArchive, err := far.NewReader(af)
	if err != nil {
		return err
	}

	b, err := pkgArchive.ReadFile("meta.far")
	if err != nil {
		return err
	}

	var tree merkle.Tree
	_, err = tree.ReadFrom(bytes.NewReader(b))
	if err != nil {
		return err
	}
	merkleroot := tree.Root()

	f, err := os.Create(filepath.Join("/pkgfs/install/pkg", fmt.Sprintf("%x", merkleroot)))
	if err != nil {
		return err
	}
	f.Truncate(int64(len(b)))
	_, err = f.Write(b)
	if err != nil {
		f.Close()
		return err
	}
	if err := f.Close(); err != nil {
		return err
	}

	for _, name := range pkgArchive.List() {
		if name == "meta.far" {
			continue
		}
		if len(name) != 64 || strings.Contains(name, "/") {
			return fmt.Errorf("Invalid package blob in archive: %s", name)
		}

		// skip blobs that the package manager does not request (it already has them)
		if _, err := os.Stat(filepath.Join("/pkgfs/needs/blobs", name)); os.IsNotExist(err) {
			continue
		}

		f, err := os.Create(filepath.Join("/pkgfs/install/blob", name))
		if err != nil {
			return err
		}
		defer f.Close()
		if err := f.Truncate(int64(pkgArchive.GetSize(name))); err != nil {
			return err
		}
		r, err := pkgArchive.Open(name)
		if err != nil {
			return err
		}
		var pos int64
		var buf [32 * 1024]byte
		for {
			n, err := r.ReadAt(buf[:], pos)
			if n > 0 {
				if _, err := f.Write(buf[:n]); err != nil {
					return err
				}
			}
			pos += int64(n)
			if err == io.EOF {
				break
			}
			if err != nil {
				return err
			}
		}
		err = f.Close()
		if err != nil {
			return err
		}
	}

	return err
}
