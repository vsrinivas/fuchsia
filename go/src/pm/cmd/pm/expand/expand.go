// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package expand implements the `pm expand` command
package expand

import (
	"bytes"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"path/filepath"
	"regexp"
	"runtime"
	"sync"
	"sync/atomic"

	"fuchsia.googlesource.com/far"
	"fuchsia.googlesource.com/pm/build"
	"fuchsia.googlesource.com/pm/pkg"
)

const metaFar = "meta.far"

const usage = `Usage: %s expand <archive>
expand a single .far representation of a package into a repository
`

var merklePat = regexp.MustCompile("^[0-9a-f]{64}$")

// Run reads an archive given in flags.Arg(1) (the first argument after
// `expand`) and unpacks the archive, adding it's contents to the appropriate
// locations in the output directory to install the package.
func Run(cfg *build.Config, args []string) error {
	fs := flag.NewFlagSet("expand", flag.ExitOnError)

	fs.Usage = func() {
		fmt.Fprintf(os.Stderr, usage, filepath.Base(os.Args[0]))
		fmt.Fprintln(os.Stderr)
		fs.PrintDefaults()
	}

	if err := fs.Parse(args); err != nil {
		return err
	}

	af, err := os.Open(fs.Arg(0))
	if err != nil {
		return err
	}
	defer af.Close()

	pkgArchive, err := far.NewReader(af)
	if err != nil {
		return err
	}

	// Make sure the path is absolute so it doesn't matter where the
	// consumers of the package are run from.
	outputDir, err := filepath.Abs(filepath.Clean(cfg.OutputDir))
	if err != nil {
		return err
	}

	if err := writeMetaAndManifest(pkgArchive, outputDir); err != nil {
		return err
	}

	if err := writeBlobsAndManifest(pkgArchive, outputDir); err != nil {
		return err
	}

	return nil
}

// Extract the meta.far to the `outputDir`, and write out a package manifest
// into `$outputDir/package.manifest`. for it. The format of the manifest is:
//
//     $PKG_NAME.$PKG_VERSION=$outputDir/meta.far
func writeMetaAndManifest(pkgArchive *far.Reader, outputDir string) error {
	// First, extract the package info from the archive, or error out if
	// the meta.far is malformed.
	p, err := extractMetaPackage(pkgArchive)
	if err != nil {
		return err
	}

	if err := os.MkdirAll(outputDir, os.ModePerm); err != nil {
		return err
	}

	if err := writeEntry(pkgArchive, outputDir, metaFar); err != nil {
		return err
	}

	f, err := os.Create(filepath.Join(outputDir, "package.manifest"))
	if err != nil {
		return err
	}
	defer f.Close()

	_, err = fmt.Fprintf(f, "%s/%s=%s\n", p.Name, p.Version, filepath.Join(outputDir, metaFar))
	if err != nil {
		return err
	}

	return nil
}

// Extract the meta-package from the archive.
func extractMetaPackage(pkgArchive *far.Reader) (*pkg.Package, error) {
	b, err := pkgArchive.ReadFile(metaFar)
	if err != nil {
		return nil, err
	}

	pkgMeta, err := far.NewReader(bytes.NewReader(b))
	if err != nil {
		return nil, err
	}

	pkgJSON, err := pkgMeta.ReadFile("meta/package")
	if err != nil {
		return nil, err
	}

	var p pkg.Package
	if err := json.Unmarshal(pkgJSON, &p); err != nil {
		return nil, err
	}

	return &p, nil
}

// Extract out all the blobs into the `outputDir`, and write out a blob
// manifest into `$outputDir/blobs.manifest`. The format of the manifest is:
//
//     $outputDir/$BLOB_MERKLE_ROOT=$BLOB_MERKLE_ROOT
func writeBlobsAndManifest(pkgArchive *far.Reader, outputDir string) error {
	blobDir := filepath.Join(outputDir, "blobs")

	// Extract out the package entries from the archive. Error out if the
	// name is not "meta.far" or a valid merkle root.
	names := []string{}
	for _, name := range pkgArchive.List() {
		if merklePat.Match([]byte(name)) {
			names = append(names, name)
		} else if name != metaFar {
			return fmt.Errorf("package contains invalid name: %q", name)
		}
	}

	if err := os.MkdirAll(blobDir, os.ModePerm); err != nil {
		return err
	}

	// Now, write all the blobs in parallel
	if err := writeEntries(pkgArchive, blobDir, names); err != nil {
		return err
	}

	f, err := os.Create(filepath.Join(outputDir, "blobs.manifest"))
	if err != nil {
		return err
	}
	defer f.Close()

	for _, name := range names {
		fmt.Fprintf(f, "%s=%s\n", filepath.Join(outputDir, "blobs", name), name)
	}

	return nil
}

// Extract the specified entries from the .far and write them to the outputDir.
func writeEntries(p *far.Reader, outputDir string, names []string) error {
	// Write out all the entries in parallel to speed things up.
	ch := make(chan string, runtime.NumCPU())

	var w sync.WaitGroup
	var hadError atomic.Value
	hadError.Store(false)
	for i := 0; i < runtime.NumCPU(); i++ {
		w.Add(1)
		go func() {
			defer w.Done()
			for name := range ch {
				if err := writeEntry(p, outputDir, name); err != nil {
					log.Printf("error writing %q: %s", name, err)
					hadError.Store(true)
					return
				}
			}
		}()
	}

	for _, name := range names {
		ch <- name
	}

	close(ch)
	w.Wait()

	if hadError.Load().(bool) {
		return fmt.Errorf("expansion finished with errors")
	}

	return nil
}

// Extract out a sepcified file from the .far and write it to the outputDir.
func writeEntry(p *far.Reader, outputDir string, name string) error {
	dst := filepath.Join(outputDir, name)
	log.Printf("writing %s to %s", name, dst)

	src, err := p.Open(name)
	if err != nil {
		return err
	}

	f, err := os.Create(dst)
	if err != nil {
		return err
	}
	defer f.Close()

	var pos int64
	var buf [32 * 1024]byte
	for {
		n, err := src.ReadAt(buf[:], pos)
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

	return nil
}
