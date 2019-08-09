// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"debug/elf"
	"encoding/json"
	"flag"
	"fmt"
	"io/ioutil"
	"os"
	"os/exec"
	"path/filepath"

	"go.fuchsia.dev/tools/command"
	"go.fuchsia.dev/tools/elflib"
	"go.fuchsia.dev/tools/runtests"
	"go.fuchsia.dev/tools/symbolize"
)

var (
	summaryFile       command.StringsFlag
	idsFile           string
	symbolizeDumpFile command.StringsFlag
	dryRun            bool
	verbose           bool
	outputDir         string
	llvmCov           string
	llvmProfdata      string
	outputFormat      string
)

func init() {
	flag.Var(&summaryFile, "summary", "path to summary.json file")
	flag.StringVar(&idsFile, "ids", "", "path to ids.txt")
	flag.Var(&symbolizeDumpFile, "symbolize-dump", "path to the json emited from the symbolizer")
	flag.BoolVar(&dryRun, "dry-run", false, "if set the system prints out commands that would be run instead of running them")
	flag.BoolVar(&verbose, "v", false, "if set the all commands will be printed out before being run")
	flag.StringVar(&outputDir, "output-dir", "", "the directory to output results to")
	flag.StringVar(&llvmProfdata, "llvm-profdata", "llvm-profdata", "the location of llvm-profdata")
	flag.StringVar(&llvmCov, "llvm-cov", "llvm-cov", "the location of llvm-cov")
	flag.StringVar(&outputFormat, "format", "html", "the output format used for llvm-cov")
}

const llvmProfileSinkType = "llvm-profile"

// Output is indexed by dump name
func readSummary(summaryFiles []string) (map[string][]runtests.DataSink, error) {
	sinks := make(map[string][]runtests.DataSink)

	for _, summaryFile := range summaryFiles {
		// TODO(phosek): process these in parallel using goroutines.
		file, err := os.Open(summaryFile)
		if err != nil {
			return nil, fmt.Errorf("cannot open %q: %v", summaryFile, err)
		}
		defer file.Close()

		var summary runtests.TestSummary
		if err := json.NewDecoder(file).Decode(&summary); err != nil {
			return nil, fmt.Errorf("cannot decode %q: %v", summaryFile, err)
		}

		dir := filepath.Dir(summaryFile)
		for _, detail := range summary.Tests {
			for name, data := range detail.DataSinks {
				for _, sink := range data {
					sinks[name] = append(sinks[name], runtests.DataSink{
						Name: sink.Name,
						File: filepath.Join(dir, sink.File),
					})
				}
			}
		}
	}

	return sinks, nil
}

type SymbolizerDump struct {
	Modules  []symbolize.Module `json:"modules"`
	SinkType string             `json:"type"`
	DumpName string             `json:"name"`
}

type SymbolizerOutput []SymbolizerDump

func readSymbolizerOutput(outputFiles []string) (map[string]SymbolizerDump, error) {
	dumps := make(map[string]SymbolizerDump)

	for _, outputFile := range outputFiles {
		// TODO(phosek): process these in parallel using goroutines.
		file, err := os.Open(outputFile)
		if err != nil {
			return nil, fmt.Errorf("cannot open %q: %v", outputFile, err)
		}
		defer file.Close()
		var output SymbolizerOutput
		if err := json.NewDecoder(file).Decode(&output); err != nil {
			return nil, fmt.Errorf("cannot decode %q: %v", outputFile, err)
		}

		for _, dump := range output {
			dumps[dump.DumpName] = dump
		}
	}

	return dumps, nil
}

// Output is indexed by build id
func readIDsTxt(idsFile string) (map[string]elflib.BinaryFileRef, error) {
	file, err := os.Open(idsFile)
	if err != nil {
		return nil, fmt.Errorf("cannot open %q: %s", idsFile, err)
	}
	defer file.Close()
	refs, err := elflib.ReadIDsFile(file)
	if err != nil {
		return nil, fmt.Errorf("cannot read %q: %v", idsFile, err)
	}
	out := make(map[string]elflib.BinaryFileRef)
	for _, ref := range refs {
		out[ref.BuildID] = ref
	}
	return out, nil
}

type indexedInfo struct {
	dumps   map[string]SymbolizerDump
	summary map[string][]runtests.DataSink
	ids     map[string]elflib.BinaryFileRef
}

type ProfileEntry struct {
	ProfileData string
	ModuleFiles []string
}

func readInfo(dumpFiles, summaryFiles []string, idsFile string) (*indexedInfo, error) {
	summary, err := readSummary(summaryFile)
	if err != nil {
		return nil, err
	}
	dumps, err := readSymbolizerOutput(symbolizeDumpFile)
	if err != nil {
		return nil, err
	}
	ids, err := readIDsTxt(idsFile)
	if err != nil {
		return nil, err
	}
	return &indexedInfo{
		dumps:   dumps,
		summary: summary,
		ids:     ids,
	}, nil
}

func mergeInfo(info *indexedInfo) ([]ProfileEntry, error) {
	entries := []ProfileEntry{}

	for _, sink := range info.summary[llvmProfileSinkType] {
		dump, ok := info.dumps[sink.Name]
		if !ok {
			fmt.Fprintf(os.Stderr, "WARN: %s not found in summary file\n", sink.Name)
			continue
		}

		// This is going to go in a covDataEntry as the list of paths to the modules for the data
		moduleFiles := []string{}
		for _, mod := range dump.Modules {
			if ref, ok := info.ids[mod.Build]; ok {
				moduleFiles = append(moduleFiles, ref.Filepath)
			} else {
				return nil, fmt.Errorf("module with build id %s not found in ids.txt file", mod.Build)
			}
		}

		// Finally we can add all the data
		entries = append(entries, ProfileEntry{
			ModuleFiles: moduleFiles,
			ProfileData: sink.File,
		})
	}

	return entries, nil
}

type Action struct {
	Path string   `json:"cmd"`
	Args []string `json:"args"`
}

func (a Action) Run() ([]byte, error) {
	if dryRun || verbose {
		fmt.Println(a.String())
	}
	if !dryRun {
		return exec.Command(a.Path, a.Args...).CombinedOutput()
	}
	return nil, nil
}

func (a Action) String() string {
	var buf bytes.Buffer
	fmt.Fprint(&buf, a.Path)
	for _, arg := range a.Args {
		fmt.Fprintf(&buf, " %s", arg)
	}
	return buf.String()
}

func isInstrumented(filepath string) bool {
	sections := []string{"__llvm_covmap", "__llvm_prf_names"}
	file, err := os.Open(filepath)
	if err != nil {
		return false
	}
	defer file.Close()
	elfFile, err := elf.NewFile(file)
	if err != nil {
		return false
	}
	for _, section := range sections {
		if sec := elfFile.Section(section); sec == nil {
			return false
		}
	}
	return true
}

func process() error {
	// Make the output directory
	err := os.MkdirAll(outputDir, os.ModePerm)
	if err != nil {
		return fmt.Errorf("creating output dir %s: %v", outputDir, err)
	}

	// Read in all the data
	info, err := readInfo(symbolizeDumpFile, summaryFile, idsFile)
	if err != nil {
		return fmt.Errorf("parsing info: %v", err)
	}

	// Merge all the information
	entries, err := mergeInfo(info)
	if err != nil {
		return fmt.Errorf("merging info: %v", err)
	}

	// Gather the set of modules and coverage files
	modSet := make(map[string]struct{})
	var mods []string
	var covFiles []string
	for _, entry := range entries {
		for _, mod := range entry.ModuleFiles {
			if _, ok := modSet[mod]; !ok && isInstrumented(mod) {
				mods = append(mods, mod)
				modSet[mod] = struct{}{}
			}
		}
		covFiles = append(covFiles, entry.ProfileData)
	}

	dir, err := ioutil.TempDir("", "covargs")
	if err != nil {
		return fmt.Errorf("cannot create temporary dir: %v", err)
	}
	defer os.RemoveAll(dir)

	// Make the llvm-profdata response file
	profdataFile, err := os.Create(filepath.Join(dir, "llvm-profdata.rsp"))
	if err != nil {
		return fmt.Errorf("creating llvm-profdata.rsp file: %v", err)
	}
	for _, covFile := range covFiles {
		fmt.Fprintf(profdataFile, "%s\n", covFile)
	}
	profdataFile.Close()

	// Merge everything
	mergedFile := filepath.Join(dir, "merged.profdata")
	mergeCmd := Action{Path: llvmProfdata, Args: []string{
		"merge",
		"-o", mergedFile,
		"@" + profdataFile.Name(),
	}}
	data, err := mergeCmd.Run()
	if err != nil {
		return fmt.Errorf("%v:\n%s", err, string(data))
	}

	// Make the llvm-cov response file
	covFile, err := os.Create(filepath.Join(dir, "llvm-cov.rsp"))
	if err != nil {
		return fmt.Errorf("creating llvm-cov.rsp file: %v", err)
	}
	for _, mod := range mods {
		fmt.Fprintf(covFile, "-object %s\n", mod)
	}
	covFile.Close()

	// Produce output
	showCmd := Action{Path: llvmCov, Args: []string{
		"show",
		"-format", outputFormat,
		"-instr-profile", mergedFile,
		"-output-dir", outputDir,
		"@" + covFile.Name(),
	}}
	data, err = showCmd.Run()
	if err != nil {
		return fmt.Errorf("%v:\n%s", err, string(data))
	}
	return nil
}

func main() {
	flag.Parse()
	if err := process(); err != nil {
		fmt.Fprintf(os.Stderr, "error: %v\n", err)
		os.Exit(1)
	}
}
