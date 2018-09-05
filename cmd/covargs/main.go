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
	"os"
	"os/exec"
	"path/filepath"

	"fuchsia.googlesource.com/tools/elflib"
	"fuchsia.googlesource.com/tools/symbolize"
)

var (
	summaryFile       string
	idsFile           string
	symbolizeDumpFile string
	dryRun            bool
	verbose           bool
	outputDir         string
	llvmCov           string
	llvmProfdata      string
	outputFormat      string
)

func init() {
	flag.StringVar(&summaryFile, "summary", "", "path to summary.json file")
	flag.StringVar(&idsFile, "ids", "", "path to ids.txt")
	flag.StringVar(&symbolizeDumpFile, "symbolize-dump", "", "path to the json emited from the symbolizer")
	flag.BoolVar(&dryRun, "dry-run", false, "if set the system prints out commands that would be run instead of running them")
	flag.BoolVar(&verbose, "v", false, "if set the all commands will be printed out before being run")
	flag.StringVar(&outputDir, "output-dir", "", "the directory to output results to")
	flag.StringVar(&llvmProfdata, "llvm-profdata", "llvm-profdata", "the location of llvm-profdata")
	flag.StringVar(&llvmCov, "llvm-cov", "llvm-cov", "the location of llvm-cov")
	flag.StringVar(&outputFormat, "format", "html", "the output format used for llvm-cov")
}

const llvmProfileSinkType = "llvm-profile"

type Test struct {
	Name      string `json:"name"`
	DataSinks map[string][]struct {
		Name string `json:"name"`
		File string `json:"file"`
	} `json:"data_sinks,omitempty"`
}

type Summary struct {
	Tests []Test `json:"tests"`
}

// Output is indexed by dump name
func readSummary(summaryFile string) (map[string]Test, error) {
	file, err := os.Open(summaryFile)
	if err != nil {
		return nil, fmt.Errorf("opening file %s: %v", summaryFile, err)
	}
	defer file.Close()
	var summary Summary
	dec := json.NewDecoder(file)
	if err := dec.Decode(&summary); err != nil {
		return nil, err
	}
	out := make(map[string]Test)
	for _, test := range summary.Tests {
		for _, profileDump := range test.DataSinks[llvmProfileSinkType] {
			out[profileDump.Name] = test
		}
	}
	return out, nil
}

type SymbolizerTriggerDump []struct {
	Mods     []symbolize.Module `json:"modules"`
	SinkType string             `json:"type"`
	DumpName string             `json:"name"`
}

func readSymbolizerDump(dumpFile string) (SymbolizerTriggerDump, error) {
	file, err := os.Open(dumpFile)
	if err != nil {
		return nil, err
	}
	defer file.Close()
	var symbolizeDump SymbolizerTriggerDump
	dec := json.NewDecoder(file)
	if err := dec.Decode(&symbolizeDump); err != nil {
		return nil, err
	}
	return symbolizeDump, nil
}

// Output is indexed by build id
func readIDsTxt(idsFile string) (map[string]elflib.BinaryFileRef, error) {
	file, err := os.Open(idsFile)
	if err != nil {
		return nil, err
	}
	defer file.Close()
	refs, err := elflib.ReadIDsFile(file)
	if err != nil {
		return nil, err
	}
	out := make(map[string]elflib.BinaryFileRef)
	for _, ref := range refs {
		out[ref.BuildID] = ref
	}
	return out, nil
}

type indexedInfo struct {
	symbolizeDump SymbolizerTriggerDump
	summary       map[string]Test
	ids           map[string]elflib.BinaryFileRef
}

type CovDataEntry struct {
	Dump        string
	Test        string
	CovDataFile string
	ModuleFiles []string
}

func readInfo(dumpFile, summaryFile, idsFile string) (*indexedInfo, error) {
	summary, err := readSummary(summaryFile)
	if err != nil {
		return nil, fmt.Errorf("parsing %s: %v", summaryFile, err)
	}
	symbolizeDump, err := readSymbolizerDump(symbolizeDumpFile)
	if err != nil {
		return nil, fmt.Errorf("parsing %s: %v", dumpFile, err)
	}
	ids, err := readIDsTxt(idsFile)
	if err != nil {
		return nil, fmt.Errorf("parsing %s: %v", idsFile, err)
	}
	return &indexedInfo{
		symbolizeDump: symbolizeDump,
		summary:       summary,
		ids:           ids,
	}, nil
}

func mergeInfo(prefix string, info *indexedInfo) ([]CovDataEntry, error) {
	out := []CovDataEntry{}
	for _, dump := range info.symbolizeDump {
		// If we get a dumpfile result that isn't from llvm-profile, ignore it
		if dump.SinkType != llvmProfileSinkType {
			continue
		}

		// Get the test data and make sure there are no errors
		test, ok := info.summary[dump.DumpName]
		if !ok {
			fmt.Fprintf(os.Stderr, "WARN: %s not found in summary file\n", dump.DumpName)
			continue
		}

		// This is going to go in a covDataEntry as the location of the coverage data
		covDataFile := filepath.Join(prefix, test.DataSinks[llvmProfileSinkType][0].File)

		// This is going to go in a covDataEntry as the list of paths to the modules for the data
		moduleFiles := []string{}
		for _, mod := range dump.Mods {
			if fileRef, ok := info.ids[mod.Build]; ok {
				moduleFiles = append(moduleFiles, fileRef.Filepath)
			} else {
				return nil, fmt.Errorf("module with build id %s not found in ids.txt file", mod.Build)
			}
		}

		// Finally we can add all the data
		out = append(out, CovDataEntry{
			Dump:        dump.DumpName,
			Test:        test.Name,
			ModuleFiles: moduleFiles,
			CovDataFile: covDataFile,
		})
	}
	return out, nil
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
	entries, err := mergeInfo(filepath.Dir(summaryFile), info)
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
		covFiles = append(covFiles, entry.CovDataFile)
	}

	// Make the mods file
	modListPath := filepath.Join(outputDir, "show.rsp")
	modList, err := os.Create(modListPath)
	if err != nil {
		return fmt.Errorf("creating mods.rsp file: %v", err)
	}
	defer modList.Close()
	for _, mod := range mods {
		fmt.Fprintf(modList, "-object %s\n", mod)
	}

	// Make the cov files file
	covFileListPath := filepath.Join(outputDir, "merge.rsp")
	covFileList, err := os.Create(covFileListPath)
	if err != nil {
		return fmt.Errorf("creating covfile.rsp file: %v", err)
	}
	defer covFileList.Close()
	for _, covFile := range covFiles {
		fmt.Fprintf(covFileList, "%s\n", covFile)
	}

	// Merge everything
	mergedFile := filepath.Join(outputDir, "merged.profdata")
	mergeCmd := Action{Path: llvmProfdata, Args: []string{
		"merge",
		"-o", mergedFile,
		"@" + covFileList.Name(),
	}}
	data, err := mergeCmd.Run()
	if err != nil {
		return fmt.Errorf("%v:\n%s", err, string(data))
	}

	// Produce output
	showCovCmd := Action{Path: llvmCov, Args: []string{
		"show",
		"-format", outputFormat,
		"-instr-profile", mergedFile,
		"-output-dir", outputDir,
		"@" + modList.Name(),
	}}
	data, err = showCovCmd.Run()
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
