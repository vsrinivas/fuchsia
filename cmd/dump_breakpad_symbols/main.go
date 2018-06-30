// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fuchsia.googlesource.com/tools/elflib"
	// TODO(kjharland): Use a safer hash algorithm. sha256 or sha2, etc.
	"crypto/sha1"
	"encoding/hex"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"os/exec"
	"path"
	"path/filepath"
	"strings"
)

const usage = `usage: dump_breakpad_symbols [options] file1 file2 ... fileN

Dumps symbol data from a collection of IDs files. IDs files are generated as
part of the build and contain a number of newline-separate records which have
the syntax:

  <hash-value> <absolute-path>

This command does not care about <hash-value>.  <absolute-path> is the path to a
binary generated as part of the Fuchsia build. This command collects every
<absolute-path> from each of file1, file2 ... fileN and dumps symbol data for
the binaries at each of those paths.  Duplicate paths are skipped.

The output is a collection of symbol files, one for each binary, using an
arbitrary naming scheme to ensure that every output file name is unique.

Example invocation:

$ dump_breakpad_symbols \
	-out-dir=/path/to/output/ \
	-dump-syms-path=/path/to/breakpad/dump_syms \
	-summary-file=/path/to/summary \
	/path/to/ids1.txt
`

// Command line flag values
var (
	summaryFilename string
	depFilename     string
	dumpSymsPath    string
	outdir          string
)

// ExecDumpSyms runs the beakpad `dump_syms` command and returns the output.
type ExecDumpSyms = func(args []string) ([]byte, error)

// CreateFile returns an io.ReadWriteCloser for the file at the given path.
type CreateFile = func(path string) (io.ReadWriteCloser, error)

func init() {
	flag.Usage = func() {
		fmt.Fprint(os.Stderr, usage)
		flag.PrintDefaults()
		os.Exit(0)
	}

	// First set the flags ...
	flag.StringVar(&summaryFilename, "summary-file", "",
		"Path to a JSON file to write that maps each binary to its symbol file. "+
			"The output looks like {'/path/to/binary': '$out-dir/path/to/file'}. ")
	flag.StringVar(&outdir, "out-dir", "",
		"The directory where symbol output should be written")
	flag.StringVar(&dumpSymsPath, "dump-syms-path", "",
		"Path to the breakpad tools `dump_syms` executable")
	flag.StringVar(&depFilename, "depfile", "",
		"Path to the ninja depfile to generate.  The file has the single line: "+
			"`OUTPUT: INPUT1 INPUT2 ...` where OUTPUT is the value of -summary-file "+
			"and INPUTX is the ids file in the same order it was provided on the "+
			"command line. -summary-file must be provided with this flag. "+
			"See `gn help depfile` for more information on depfiles.")
}

func main() {
	flag.Parse()

	// Create and open depfile.
	depFile, err := os.Create(depFilename)
	if err != nil {
		log.Fatalf("could not create file %s: %v", depFilename, err)
	}
	defer depFile.Close()

	// Create and open summary file.
	summaryFile, err := os.Create(summaryFilename)
	if err != nil {
		log.Fatalf("could not create file %s: %v", summaryFilename, err)
	}
	defer summaryFile.Close()

	// Callback to run breakpad `dump_syms` command.
	execDumpSyms := func(args []string) ([]byte, error) {
		return exec.Command(dumpSymsPath, args...).Output()
	}

	// Callback to create new files.
	createFile := func(path string) (io.ReadWriteCloser, error) {
		return os.Create(path)
	}

	// Open the input files for reading.  In practice there are very few files,
	// so it's fine to open them all at once.
	var inputReaders []io.Reader
	inputPaths := flag.Args()
	for _, path := range inputPaths {
		file, err := os.Open(path)
		if err != nil {
			fmt.Fprintf(os.Stderr, "ERROR: Failed to open %s: %v\n", path, err)
			os.Exit(1)
		}
		defer file.Close()
		inputReaders = append(inputReaders, file)
	}

	// Process the IDsFiles.
	summary := processIdsFiles(inputReaders, outdir, execDumpSyms, createFile)

	// Write the summary.
	if err := writeSummary(summaryFile, summary); err != nil {
		fmt.Fprintf(os.Stderr, "failed to write summary %s: %v", summaryFilename, err)
		os.Exit(1)
	}
	// Write the dep file.
	if err := writeDepFile(depFile, summaryFilename, inputPaths); err != nil {
		fmt.Fprintf(os.Stderr, "failed to write depfile %s: %v", depFilename, err)
		os.Exit(1)
	}
}

// processIdsFiles dumps symbol data for each executable in a set of ids files.
func processIdsFiles(idsFiles []io.Reader, outdir string, execDumpSyms ExecDumpSyms, createFile CreateFile) map[string]string {
	// Binary paths we've already seen.  Duplicates are skipped.
	visited := make(map[string]bool)
	binaryToSymbolFile := make(map[string]string)

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

			// Generate the symbol file path.
			symbolFilepath := createSymbolFilepath(outdir, binaryPath)

			// Record the mapping in the summary.
			binaryToSymbolFile[binaryPath] = symbolFilepath

			log.Printf("dumping symbols for %s into %s\n", binaryPath, symbolFilepath)

			// Generate the symbol data.
			symbolData, err := execDumpSyms([]string{binaryPath})
			if err != nil {
				log.Printf("ERROR: failed to generate symbol data for %s: %v", binaryPath, err)
				continue
			}

			// Write the symbol file.
			symbolFile, err := createFile(symbolFilepath)
			if err != nil {
				log.Printf("ERROR: failed to create symbol file %s: %v", symbolFilepath, err)
				continue
			}
			if err := writeSymbolFile(symbolFile, symbolData); err != nil {
				symbolFile.Close()
				log.Printf("ERROR: failed to write symbol file %s: %v", symbolFilepath, err)
				continue
			}
			symbolFile.Close()
		}
	}

	return binaryToSymbolFile
}

// writeSummary writes the summary file to the given io.Writer.
func writeSummary(w io.Writer, summary map[string]string) error {
	// TODO(kjharland): Sort the keys before printing to ensure predictable
	// output or use a different data structure with a consistent ordering.

	// Serialize the summary.
	summaryBytes, err := json.MarshalIndent(summary, "", "  ")
	if err != nil {
		return fmt.Errorf("failed to marshal summary: %v", err)
	}

	// Write the summary.
	if _, err := w.Write(summaryBytes); err != nil {
		return fmt.Errorf("write failed: %v", err)
	}

	return nil
}

// writeDepFile writes a ninja dep file to the given io.Writer.
//
// summaryPath is the summary file generated by this command.
// IDsFiles are the input sources to this command.
func writeDepFile(w io.Writer, summaryFilename string, IDsFiles []string) error {
	contents := []byte(fmt.Sprintf("%s: %s\n", summaryFilename, strings.Join(IDsFiles, " ")))
	_, err := w.Write(contents)
	return err
}

// Writes the given symbol file data to the given writer after massaging the data.
func writeSymbolFile(w io.Writer, symbolData []byte) error {
	// Many Fuchsia binaries are built as "something.elf", but then packaged as
	// just "something". In the ids.txt file, the name still includes the ".elf"
	// extension, which dump_syms emits into the .sym file, and the crash server
	// uses as part of the lookup.  The binary name and this value written to
	// the .sym file must match, so if the first header line ends in ".elf"
	// strip it off.  This line usually looks something like:
	// MODULE Linux x86_64 094B63014248508BA0636AD3AC3E81D10 sysconf.elf
	lines := strings.SplitN(string(symbolData), "\n", 2)
	if len(lines) != 2 {
		return fmt.Errorf("got <2 lines in symbol data")
	}

	// Make sure the first line is not empty.
	lines[0] = strings.TrimSpace(lines[0])
	if lines[0] == "" {
		return fmt.Errorf("unexpected blank first line in symbol data")
	}

	// Strip .elf from header if it exists.
	if strings.HasSuffix(lines[0], ".elf") {
		lines[0] = strings.TrimSuffix(lines[0], ".elf")
		// Join the new lines of the symbol data.
		symbolData = []byte(strings.Join(lines, "\n"))
	}

	// Write the symbol file.
	_, err := w.Write(symbolData)
	return err
}

// Creates the absolute path to the symbol file for the given binary.
//
// The returned path is generated as a subpath of parentDir.
func createSymbolFilepath(parentDir string, binaryPath string) string {
	// Create the symbole file basename as a hash of the path to the binary.
	// This ensures that filenames are unique within the output directory.
	hash := sha1.New()
	n, err := hash.Write([]byte(binaryPath))
	if err != nil {
		panic(err)
	}
	if n == 0 {
		// Empty text should never be passed to this function and likely signifies
		// an error in the input file. Panic here as well.
		panic("0 bytes written for hash of input text '" + binaryPath + "'")
	}
	basename := hex.EncodeToString(hash.Sum(nil)) + ".sym"

	// Generate the filepath as an subdirectory of the given parent directory.
	absPath, err := filepath.Abs(path.Join(parentDir, basename))
	if err != nil {
		// Panic because if this fails once it's likely to keep failing.
		panic(fmt.Sprintf("failed to get path to symbol file for %s: %v", binaryPath, err))
	}

	return absPath
}
