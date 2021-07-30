// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"
	"io/fs"
	"os"
	"path"
	"path/filepath"
	"regexp"
	"sort"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/mdlint/core"
	_ "go.fuchsia.dev/fuchsia/tools/mdlint/rules"
)

type dirFlag string

func (flag *dirFlag) String() string {
	return "dir"
}

func (flag *dirFlag) Set(dir string) error {
	dirFs, err := os.Stat(dir)
	if err != nil {
		return err
	}
	if !dirFs.Mode().IsDir() {
		return fmt.Errorf("%s: not a directory", dir)
	}
	*flag = dirFlag(dir)
	return nil
}

type enabledRulesFlag []string

func (flag *enabledRulesFlag) String() string {
	return "name(s)"
}

func (flag *enabledRulesFlag) Set(name string) error {
	if name != core.AllRulesName && !core.HasRule(name) {
		return fmt.Errorf("unknown rule '%s'", name)
	}
	*flag = append(*flag, name)
	return nil
}

var (
	rootDir                 dirFlag
	reportFilenamesMatching string
	jsonOutput              bool
	enabledRules            enabledRulesFlag
)

func init() {
	flag.Var(&rootDir, "root-dir", "(required) Path to root directory containing Markdown files")
	flag.StringVar(&reportFilenamesMatching, "filter-filenames", "", "Regex to filter warnings by their filenames")
	flag.BoolVar(&jsonOutput, "json", false, "Enable JSON output")

	var names []string
	for _, name := range core.AllRules() {
		names = append(names, fmt.Sprintf("'%s'", name))
	}
	sort.Strings(names)
	flag.Var(&enabledRules, "enable", fmt.Sprintf(
		"Enable a rule. Valid rules are %s. To enable all rules, use the special '%s' name",
		strings.Join(names, ", "), core.AllRulesName))
}

func printUsage() {
	program := path.Base(os.Args[0])
	message := `Usage: ` + program + ` [flags]

Markdown linter.

Flags:
`
	fmt.Fprint(flag.CommandLine.Output(), message)
	flag.PrintDefaults()
}

const (
	exitOnSuccess = 0
	exitOnError   = 1
)

func processAllDocs(rules core.LintRuleOverTokens, filenames []string) error {
	rules.OnStart()
	defer rules.OnEnd()

	for _, filename := range filenames {
		if err := func() error {
			file, err := os.Open(filename)
			if err != nil {
				return err
			}
			defer file.Close()
			return core.ProcessSingleDoc(filename, file, rules)
		}(); err != nil {
			return err
		}
	}
	return nil
}

func allMdFilenames() ([]string, error) {
	var filenames []string
	if err := filepath.WalkDir(string(rootDir), func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if filepath.Ext(path) == ".md" {
			filenames = append(filenames, path)
		}
		return nil
	}); err != nil {
		return nil, err
	}
	return filenames, nil
}

func main() {
	flag.Usage = printUsage
	flag.Parse()
	if !flag.Parsed() || rootDir == "" {
		printUsage()
		os.Exit(exitOnError)
	}

	reporter := core.RootReporter{
		JSONOutput: jsonOutput,
	}
	rules := core.InstantiateRules(&reporter, enabledRules)
	filenames, err := allMdFilenames()
	if err != nil {
		fmt.Fprintf(os.Stderr, "%s\n", err)
		os.Exit(exitOnError)
	}

	if err := processAllDocs(rules, filenames); err != nil {
		fmt.Fprintf(os.Stderr, "%s\n", err)
		os.Exit(exitOnError)
	}

	filenamesFilter := regexp.MustCompile(reportFilenamesMatching)
	if reporter.HasMessages(filenamesFilter) {
		reporter.PrintOnlyForFiles(filenamesFilter, os.Stderr)
		os.Exit(exitOnError)
	}

	os.Exit(exitOnSuccess)
}
