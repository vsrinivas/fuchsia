// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	// TODO(kjharland): change crypto/sha1 to a safer hash algorithm. sha256 or sha2, etc.
	"archive/tar"
	"compress/gzip"
	"context"
	"flag"
	"fmt"
	"log"
	"os"

	"go.fuchsia.dev/fuchsia/tools/debug/breakpad/generator"
	"go.fuchsia.dev/fuchsia/tools/debug/elflib"
	"go.fuchsia.dev/fuchsia/tools/lib/tarutil"
)

const usage = `usage: dump_breakpad_symbols [options] paths...

Generates breakpad symbol files by running dump_syms on a collection of binaries. Produces
a tar archive containing all generated files. Expects a list of paths to .build-id dirs as
input.

Example invocation:

$ dump_breakpad_symbols -dump-syms-path=dump_syms -tar-file=out.tar.gz -depfile=dep.out ./.build-ids
`

// Command line flag values
var (
	depFilepath  string
	dumpSymsPath string
	tarFilepath  string

	// Flag to switch to using .build-id directory paths as input instead of ids.txt
	// files.
	//
	// TODO(1068): Make this the default and delete support for ids.txt.
	useBuildIDInput bool
)

func init() {
	flag.Usage = func() {
		fmt.Fprint(os.Stderr, usage)
		flag.PrintDefaults()
		os.Exit(0)
	}

	var unused string
	flag.StringVar(&unused, "out-dir", "", "DEPRECATED. This is not used")
	flag.StringVar(&dumpSymsPath, "dump-syms-path", "", "Path to the breakpad tools `dump_syms` executable")
	flag.StringVar(&depFilepath, "depfile", "", "Path to the ninja depfile to generate")
	flag.StringVar(&tarFilepath, "tar-file", "", "Path where the tar archive containing symbol files is written")
	flag.BoolVar(&useBuildIDInput, "use-build-id", false, "Use .build-id dir inputs instead of ids.txt")
}

func main() {
	flag.Parse()
	if err := execute(context.Background(), flag.Args()...); err != nil {
		log.Fatal(err)
	}
}

func execute(ctx context.Context, paths ...string) error {
	// Collect all binary file refs from each directory
	var bfrs []elflib.BinaryFileRef
	var err error
	if useBuildIDInput {
		bfrs, err = bfrsFromBuildIDs(paths...)
	} else {
		bfrs, err = bfrsFromIdsTxt(paths...)
	}
	if err != nil {
		return err
	}

	// Skip files without .debug_info header.
	bfrs = filterEmptyDebugSymbolFiles(bfrs)
	// Generate all symbol files.
	path, err := generator.Generate(bfrs, dumpSymsPath)
	if err != nil {
		return fmt.Errorf("failed to generate symbols: %w", err)
	}

	// Write all files to the specified tar archive.
	tarfd, err := os.Create(tarFilepath)
	if err != nil {
		return fmt.Errorf("failed to create %q: %w", tarFilepath, err)
	}
	gzw := gzip.NewWriter(tarfd)
	defer gzw.Close()
	tw := tar.NewWriter(gzw)
	defer tw.Close()

	log.Printf("archiving %q to %q", path, tarFilepath)
	if err := tarutil.TarDirectory(tw, path); err != nil {
		return fmt.Errorf("failed to write %q: %w", tarFilepath, err)
	}

	// Write the Ninja dep file.
	depfile := depfile{outputPath: tarFilepath, inputPaths: paths}
	depfd, err := os.Create(depFilepath)
	if err != nil {
		return fmt.Errorf("failed to create %q: %w", depFilepath, err)
	}
	n, err := depfile.WriteTo(depfd)
	if err != nil {
		return fmt.Errorf("failed to write %q: %w", depFilepath, err)
	}
	if n == 0 {
		return fmt.Errorf("wrote 0 bytes to %q", depFilepath)
	}
	return nil
}

// TODO(1068): Delete this after updating the build to use .build-id directories.
func bfrsFromIdsTxt(paths ...string) ([]elflib.BinaryFileRef, error) {
	var bfrs []elflib.BinaryFileRef
	for _, path := range paths {
		fd, err := os.Open(path)
		if err != nil {
			return nil, fmt.Errorf("failed to open %q: %w", path, err)
		}
		defer fd.Close()
		newbfrs, err := elflib.ReadIDsFile(fd)
		if err != nil {
			return nil, fmt.Errorf("failed to read %q: %w", path, err)
		}
		bfrs = append(bfrs, newbfrs...)
	}
	return bfrs, nil
}

func bfrsFromBuildIDs(dirs ...string) ([]elflib.BinaryFileRef, error) {
	var bfrs []elflib.BinaryFileRef
	for _, dir := range dirs {
		newbfrs, err := elflib.WalkBuildIDDir(dir)
		if err != nil {
			return nil, err
		}
		bfrs = append(bfrs, newbfrs...)
	}
	return bfrs, nil
}

// Returns filtered input of BinaryFileRefs, skipping files without .debug_info header.
func filterEmptyDebugSymbolFiles(bfrs []elflib.BinaryFileRef) []elflib.BinaryFileRef {
	var filteredBfrs []elflib.BinaryFileRef
	for _, bfr := range bfrs {
		hasDebugInfo, err := bfr.HasDebugInfo()
		if err != nil {
			log.Printf("WARNING: cannot read file %s: %v, skipping\n", bfr.Filepath, err)
		} else if !hasDebugInfo {
			log.Printf("WARNING: file %s missing .debug_info section, skipping\n", bfr.Filepath)
		} else {
			filteredBfrs = append(filteredBfrs, bfr)
		}
	}
	return filteredBfrs
}
