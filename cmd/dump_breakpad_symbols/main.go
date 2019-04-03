// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	// TODO(kjharland): change crypto/sha1 to a safer hash algorithm. sha256 or sha2, etc.
	"archive/tar"
	"bytes"
	"compress/gzip"
	"context"
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"os/exec"

	"fuchsia.googlesource.com/tools/breakpad"
	"fuchsia.googlesource.com/tools/elflib"
	"fuchsia.googlesource.com/tools/tarutil"
)

const usage = `usage: dump_breakpad_symbols [options] paths...

Generates breakpad symbol files by running dump_syms on a collection of binaries. Produces
a tar archive containing all generated files. Expects a list of paths to .build-id dirs as
input.

Example invocation:

$ dump_breakpad_symbols -dump-syms-path=dump_syms -tar-file=out.tar.gz -depfile=dep.out ./.build-ids
`

// The default module name for modules that don't have a soname, e.g., executables and
// loadable modules. This allows us to use the same module name at runtime as sonames are
// the only names that are guaranteed to be available at build and run times. This value
// must be kept in sync with what Crashpad uses at run time for symbol resolution to work
// properly.
const defaultModuleName = "<_>"

// Command line flag values
var (
	depFilepath  string
	dumpSymsPath string
	tarFilepath  string
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
}

func main() {
	flag.Parse()
	if err := execute(context.Background()); err != nil {
		log.Fatal(err)
	}
}

func execute(ctx context.Context) error {
	// Open the input files for reading.  In practice there are very few files,
	// so it's fine to open them all at once.
	var inputReaders []io.Reader
	inputPaths := flag.Args()
	for _, path := range inputPaths {
		file, err := os.Open(path)
		if err != nil {
			return fmt.Errorf("failed to open %s: %v\n", path, err)
		}
		defer file.Close()
		inputReaders = append(inputReaders, file)
	}

	// Process the IDsFiles.
	symbolFiles := processIdsFiles(inputReaders)

	// Write the Ninja dep file.
	depfile := depfile{outputPath: tarFilepath, inputPaths: inputPaths}
	depfd, err := os.Create(depFilepath)
	if err != nil {
		return fmt.Errorf("failed to create file %q: %v", depFilepath, err)
	}
	n, err := depfile.WriteTo(depfd)
	if err != nil {
		return fmt.Errorf("failed to write Ninja dep file %q: %v", depFilepath, err)
	}
	if n == 0 {
		return fmt.Errorf("wrote 0 bytes to %q", depFilepath)
	}

	// Write the tar archive containing all symbol files.
	tarfd, err := os.Create(tarFilepath)
	if err != nil {
		return fmt.Errorf("failed to create %q: %v", tarFilepath, err)
	}
	gzw := gzip.NewWriter(tarfd)
	defer gzw.Close()
	tw := tar.NewWriter(gzw)
	defer tw.Close()
	for sf := range symbolFiles {
		if err := tarutil.TarReader(tw, sf.Reader(), sf.ModuleSection.BuildID); err != nil {
			return fmt.Errorf("failed to archive %q: %v", sf.ModuleSection.BuildID, err)
		}
	}
	return nil
}

// processIdsFiles dumps symbol data for each executable in a set of ids files.
func processIdsFiles(idsFiles []io.Reader) <-chan breakpad.SymbolFile {
	output := make(chan breakpad.SymbolFile, 10000) // Arbitrary capacity.

	go func() {
		// Binary paths we've already seen.  Duplicates are skipped.
		visited := make(map[string]bool)

		// Iterate through the given set of filepaths.
		for _, idsFile := range idsFiles {
			// Extract the paths to each binary from the IDs file.
			binaries, err := elflib.ReadIDsFile(idsFile)
			if err != nil {
				fmt.Fprintln(os.Stderr, err)
				continue
			}

			// Generate the symbol file for each binary.
			for _, bin := range binaries {
				binaryPath := bin.Filepath

				// Check whether we've seen this path already. Skip if so.
				if _, ok := visited[binaryPath]; ok {
					continue
				}
				// Record that we've seen this binary path.
				visited[binaryPath] = true

				sf, err := generateSymbolFile(binaryPath)
				if err != nil {
					log.Println(err)
					continue
				}

				output <- *sf
			}
		}
		close(output)
	}()

	return output
}

func generateSymbolFile(path string) (*breakpad.SymbolFile, error) {
	var stdout bytes.Buffer
	cmd := exec.Cmd{
		Path:   dumpSymsPath,
		Args:   []string{dumpSymsPath, path},
		Stdout: &stdout,
	}
	if err := cmd.Run(); err != nil {
		return nil, fmt.Errorf("cmd failed %v: %v", cmd.Args, err)
	}

	symbolFile, err := breakpad.ParseSymbolFile(&stdout)
	if err != nil {
		return nil, fmt.Errorf("failed to parse dump_syms output: %v", err)
	}

	// Ensure the module name is either the soname (for shared libraries) or the default
	// value (for executables and loadable modules).
	fd, err := os.Open(path)
	if err != nil {
		return nil, fmt.Errorf("failed to open %q: %v", path, err)
	}
	defer fd.Close()
	soname, err := elflib.GetSoName(path, fd)
	if err != nil {
		return nil, fmt.Errorf("failed to read soname from %q: %v", path, err)
	}
	if soname == "" {
		symbolFile.ModuleSection.ModuleName = defaultModuleName
	} else {
		symbolFile.ModuleSection.ModuleName = soname
	}

	// Ensure the module section specifies this is a Fuchsia binary instead of Linux
	// binary, which is the default for the dump_syms tool.
	symbolFile.ModuleSection.OS = "Fuchsia"
	return symbolFile, nil
}
