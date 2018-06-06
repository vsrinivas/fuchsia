// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	// TODO(kjharland): Use a safer hash algorithm. sha256 or sha2, etc.
	"crypto/sha1"
	"encoding/hex"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"os/exec"
	"path"
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

// Options represents the command line options.
type Options struct {
	depFile      string
	dryRun       bool
	dumpSymsPath string
	outdir       string
	summaryFile  string
}

func main() {
	RunMain(os.Args)
}

// RunMain implements the main() function. Visible for testing.
// TODO(kjharland): Do this in init() and test this library directly rather
// than from a separate main_test package.
func RunMain(args []string) {
	f, options, err := ParseFlags(args)
	if err != nil {
		log.Fatal(err)
	}

	if processIdsFiles(f.Args(), options) {
		log.Println("finished with errors")
		os.Exit(1)
	}
}

// ParseFlags parses command line parameters. Visible for testing.
func ParseFlags(args []string) (*flag.FlagSet, *Options, error) {
	f := flag.NewFlagSet(args[0], flag.ContinueOnError)
	f.Usage = func() {
		fmt.Println(usage)
		flag.PrintDefaults()
		os.Exit(0)
	}

	var options Options
	// First set the flags ...
	f.StringVar(&options.summaryFile, "summary-file", "",
		"Path to a JSON file to write that maps each binary to its symbol file. "+
			"The output looks like {'/path/to/binary': '$out-dir/path/to/file'}. "+
			"Prints to stdout by default.",
	)
	f.StringVar(&options.outdir, "out-dir", "",
		"The directory where symbol output should be written")
	f.StringVar(&options.dumpSymsPath, "dump-syms-path", "",
		"Path to the breakpad tools `dump_syms` executable")
	f.BoolVar(&options.dryRun, "dry-run", false,
		"Print the dump_syms commands to run, without running them, then exit. "+
			"summary-file is always written to stdout during a dry-run.",
	)
	f.StringVar(&options.depFile, "depfile", "",
		"Path to the ninja depfile to generate.  The file has the single line: "+
			"`OUTPUT: INPUT1 INPUT2 ...` where OUTPUT is the value of -summary-file "+
			"and INPUTX is the ids file in the same order it was provided on the "+
			"command line. -summary-file must be provided with this flag. "+
			"See `gn help depfile` for more information on depfiles.")
	f.Parse(args[1:])

	// Ensure at least one file was given.
	if f.NArg() < 1 {
		return nil, nil, errors.New("at least one ids.txt file is required")
	}
	// Ensure path to dump_syms is specified
	if options.dumpSymsPath == "" {
		return nil, nil, errors.New("-dump-syms-path is required")
	}
	// Ensure output directory was given.
	if options.outdir == "" {
		return nil, nil, errors.New("-out-dir is required")
	}
	// Ensure summary file was provided if -depfile was provided.
	if options.depFile != "" && options.summaryFile == "" {
		return nil, nil, errors.New("must specify -summary-file with -depfile")
	}

	return f, &options, nil
}

// processidsFiles dumps symbol data for each executable in a set of ids files.
//
// Returns true iff any errors occurred.
func processIdsFiles(idsFiles []string, options *Options) (gotErrors bool) {
	// Indicates whether we've seen a binary path already.  Duplicate paths are
	// skipped.
	visited := make(map[string]bool)
	binaryToSymbolFile := make(map[string]string)

	// Confirm that the user is performing a dry-run.
	if options.dryRun {
		log.Println("Performing dry-run")
	}

	// Iterate through the given set of filepaths.
	for _, idsFile := range idsFiles {
		// Extract the paths to each binary from the IDs file.
		binaryPaths, err := extractBinaryPaths(idsFile)
		if err != nil {
			logError("failed to extract paths from %s: %v", idsFile, err)
			gotErrors = true
			continue
		}

		// Generate symbol data for each binary.
		for _, binaryPath := range binaryPaths {
			// Check whether we've seen this path already. Skip if so.
			if _, ok := visited[binaryPath]; ok {
				continue
			}
			// Record that we've seen this binary path.
			visited[binaryPath] = true

			// Generate the symbol file path.
			symbolFile := path.Join(options.outdir, hashText(binaryPath)+".sym")

			// Record the mapping in the summary.
			binaryToSymbolFile[binaryPath] = symbolFile

			// Log what we're about to do. If this a dry run, say so and
			// continue without dumping symbols.
			info := fmt.Sprintf("dumping symbols for %s into %s", binaryPath, symbolFile)
			if options.dryRun {
				log.Println("DRY_RUN: " + info)
				continue
			}
			log.Println(info)

			// Dump the symbol data to disk. Record an error
			if err := dumpSymbolData(binaryPath, symbolFile, options.dumpSymsPath); err != nil {
				logError("%v", err)
				gotErrors = true
				continue
			}
		}
	}

	var summaryFile *os.File

	// If no summary file path was given, write the summmary to stdout.
	if options.summaryFile == "" || options.dryRun {
		summaryFile = os.Stdout
	} else {
		var err error
		summaryFile, err = os.Create(options.summaryFile)
		if err != nil {
			logError("failed to open summary file %s: %v", options.summaryFile, err)
			gotErrors = true
			return
		}
	}

	if err := writeSummary(binaryToSymbolFile, summaryFile); err != nil {
		logError("failed to output summary %s: %v", options.summaryFile, err)
		gotErrors = true
		return
	}

	// Write the depfile if specified.
	if options.depFile != "" {
		if options.summaryFile == "" {
			// If we get here there's a bug in the flag parsing.  Summary file
			// should always be specified with Dep file.
			logError("cannot print dep file without summary file path")
			gotErrors = true
			return
		}

		// Write the file.
		depFileContents := []byte(
			fmt.Sprintf("%s: %s\n", options.summaryFile, strings.Join(idsFiles, " ")))
		if err := ioutil.WriteFile(options.depFile, depFileContents, 0644); err != nil {
			logError("failed to write dep file %s: %v", options.depFile, err)
			gotErrors = true
		}
	}

	return
}

// Returns a sha1 hash of the input text.
func hashText(text string) string {
	hash := sha1.New()
	n, err := hash.Write([]byte(text))
	if err != nil {
		panic(err)
	}
	if n == 0 {
		// Empty text should never be passed to this function and likely signifies
		// an error in the input file. Panic here as well.
		panic("0 bytes written for hash of input text '" + text + "'")
	}

	return hex.EncodeToString(hash.Sum(nil))
}

// Logs an error message.
//
// format and args work the same as with fmt.Printf.
func logError(format string, args ...interface{}) {
	log.Printf("ERROR: "+format, args...)
}

// Writes the summary.
func writeSummary(summary map[string]string, file *os.File) error {
	// TODO(kjharland): Sort the keys before priting to ensure predictable
	// output or use a different data structure with a consistent ordering.

	// Serialize the summary.
	summaryBytes, err := json.MarshalIndent(summary, "", "  ")
	if err != nil {
		return fmt.Errorf("json marhsal failed: %v", err)
	}

	// Write the summary.
	if _, err := file.Write(summaryBytes); err != nil {
		return fmt.Errorf("write failed: %v", err)
	}

	return nil
}

// Extracts a list of absolute paths to binaries from an idsFile.
//
// See the helptext for this command for info about the idsFile.  This function
// handles malformed input gracefully, logging errors rather than returning
// early.
//
// Returns the list of binary paths.
//
// TODO(kjharland): Use https://fuchsia.googlesource.com/tools/+/master/symbolize/repo.go
// and delete this.
func extractBinaryPaths(idsFile string) ([]string, error) {
	var binaryPaths []string

	// Read file contents.
	idsFileBytes, err := ioutil.ReadFile(idsFile)
	if err != nil {
		return nil, err
	}

	idsFileLines := strings.Split(string(idsFileBytes), "\n")

	// Extract the path to the binary from each line.
	for _, line := range idsFileLines {
		line = strings.TrimSpace(line)
		if len(line) == 0 {
			// Skip empty lines gracefully.
			continue
		}

		fields := strings.Split(line, " ")
		if len(fields) != 2 {
			// Lines should only have two columns. Abort if input is malformed.
			return nil, fmt.Errorf("malformed line in %s: %s", idsFile, line)
		}

		binaryPaths = append(binaryPaths, fields[1])
	}

	return binaryPaths, nil
}

// Runs the breakpad tool `dump_syms` on the binary at the given absolute path,
// Then writes the symbol data to the given path.
func dumpSymbolData(binaryPath, symbolFile, dumpSymsPath string) error {
	// Run the dump_syms command.
	symbolData, err := exec.Command(dumpSymsPath, binaryPath).Output()
	if err != nil {
		return fmt.Errorf("failed to execute %s: %s", dumpSymsPath, err)
	}

	// Many Fuchsia binaries are built as "something.elf", but then packaged as
	// just "something". In the ids.txt file, the name still includes the ".elf"
	// extension, which dump_syms emits into the .sym file, and the crash server
	// uses as part of the lookup.  The binary name and this value written to
	// the .sym file must match, so if the first header line ends in ".elf"
	// strip it off.  This line usually looks something like:
	// MODULE Linux x86_64 094B63014248508BA0636AD3AC3E81D10 sysconf.elf
	lines := strings.SplitN(string(symbolData), "\n", 2)
	if len(lines) != 2 {
		return fmt.Errorf("got <2 lines in symbol data for %s", binaryPath)
	}

	// Make sure the first line is not empty.
	lines[0] = strings.TrimSpace(lines[0])
	if lines[0] == "" {
		return fmt.Errorf("unexpected blank first line in symbol data for %s", binaryPath)
	}

	// Strip .elf from header if it exists.
	if strings.HasSuffix(lines[0], ".elf") {
		lines[0] = strings.TrimSuffix(lines[0], ".elf")
		// Join the new lines of the symbol data.
		symbolData = []byte(strings.Join(lines, "\n"))
	}

	// Write the symbol file.
	if err := ioutil.WriteFile(symbolFile, []byte(symbolData), 0644); err != nil {
		return fmt.Errorf("could not write output file %s: %v", symbolFile, err)
	}

	return nil
}
