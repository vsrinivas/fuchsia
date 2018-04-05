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
	"log"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"sync"
	"sync/atomic"

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

	names := make(chan string, runtime.NumCPU())

	var w sync.WaitGroup
	var hadError atomic.Value
	hadError.Store(false)
	for i := 0; i < runtime.NumCPU(); i++ {
		w.Add(1)
		go func() {
			defer w.Done()
			for name := range names {
				func() {
					if len(name) != 64 || strings.Contains(name, "/") {
						log.Printf("Invalid package blob in archive: %s", name)
						hadError.Store(true)
						return
					}

					// skip blobs that the package manager does not request (it already has them)
					if _, err := os.Stat(filepath.Join("/pkgfs/needs/blobs", name)); os.IsNotExist(err) {
						return
					}

					// TODO(raggi): support a non-exclusive write where the blob exists but isn't readable, try to write it from the archive
					f, err := os.OpenFile(filepath.Join("/pkgfs/install/blob", name), os.O_WRONLY|os.O_CREATE|os.O_EXCL, os.ModePerm)
					if os.IsExist(err) {
						return
					}
					if err != nil {
						log.Printf("Error creating blob in blobfs: %s", err)
						hadError.Store(true)
						return
					}
					defer f.Close()

					size := int64(pkgArchive.GetSize(name))
					r, err := pkgArchive.Open(name)
					if err != nil {
						log.Printf("Error opening archive file %q: %s", name, err)
						hadError.Store(true)
						return
					}

					if err := f.Truncate(size); err != nil {
						log.Printf("Error truncating blob %q to %d: %s", name, size, err)
						hadError.Store(true)
						return
					}

					var pos int64
					var buf [32 * 1024]byte
					for {
						n, err := r.ReadAt(buf[:], pos)
						if n > 0 {
							if _, err := f.Write(buf[:n]); err != nil {
								log.Printf("Error writing to blob %q: %s", name, err)
								hadError.Store(true)
								return
							}
						}
						pos += int64(n)
						if err == io.EOF {
							break
						}
						if err != nil {
							log.Printf("Error reading %q from archive: %s", name, err)
							hadError.Store(true)
							return
						}
					}
					err = f.Close()
					if err != nil {
						log.Printf("Error closing blob %q: %s", name, err)
						hadError.Store(true)
						return
					}
				}()
			}
		}()
	}

	for _, name := range pkgArchive.List() {
		if name == "meta.far" {
			continue
		}
		names <- name
	}
	close(names)
	w.Wait()

	if hadError.Load().(bool) {
		return fmt.Errorf("installation finished with errors")
	}

	return nil
}
