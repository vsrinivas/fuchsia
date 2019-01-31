///bin/true ; exec /usr/bin/env go run "$0" "$@"
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bufio"
	"flag"
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"os/exec"
	"path"
	"path/filepath"
	"sort"
	"strings"
)

var dryRun = flag.Bool("n", false, "Dry run - print what would happen but don't actually do it")
var symbolDir = flag.String("symbol-dir", "", "Location for the symbol files")
var upload = flag.Bool("upload", true, "Whether to upload the dumped symbols")
var url = flag.String("url", "https://clients2.google.com/cr/symbol", "Endpoint to use")
var verbose = flag.Bool("v", false, "Verbose output")
var x64Symbols = flag.Bool("x64-symbols", true, "Include symbols from x64 build")
var armSymbols = flag.Bool("arm-symbols", true, "Include symbols from arm64 build")

func getBinariesFromIds(idsFilename string) sort.StringSlice {
	var binaries []string
	file, err := os.Open(idsFilename)
	if err != nil {
		log.Fatal(err)
	}
	defer file.Close()

	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		binary := strings.SplitAfterN(scanner.Text(), " ", 2)[1]
		binaries = append(binaries, binary)
	}

	if err := scanner.Err(); err != nil {
		log.Fatal(err)
	}

	return binaries
}

func dump(bin, fuchsiaRoot string) string {
	if *verbose || *dryRun {
		fmt.Println("Dumping binary", bin)
	}
	relToRoot, err := filepath.Rel(fuchsiaRoot, bin)
	if err != nil {
		log.Fatal("could not filepath.Rel ", bin, ": ", err)
	}
	symfile := path.Join(*symbolDir, strings.Replace(relToRoot, "/", "#", -1)+".sym")
	if *dryRun {
		return symfile
	}
	out, err := exec.Command("buildtools/linux-x64/dump_syms/dump_syms", bin).Output()
	if err != nil {
		log.Fatal("could not dump_syms ", bin, ": ", err)
	}

	// Many Fuchsia binaries are built as "something.elf", but then packaged as
	// just "something". In the ids.txt file, the name still includes the ".elf"
	// extension, which dump_syms emits into the .sym file, and the crash server
	// uses as part of the lookup (that is, both the name and the buildid have to
	// match). So, if the first header line ends in ".elf" strip it off.
	lines := strings.Split(string(out), "\n")
	lines[0] = strings.TrimSuffix(lines[0], ".elf")
	out = []byte(strings.Join(lines, "\n"))

	err = ioutil.WriteFile(symfile, out, 0644)
	if err != nil {
		log.Fatal("could not write output file", symfile, ": ", err)
	}

	return symfile
}

func uploadSymbols(symfile string) {
	if *verbose || *dryRun {
		fmt.Println("Uploading symbols", symfile)
	}
	if *dryRun {
		return
	}

	out, err := exec.Command("buildtools/linux-x64/symupload/sym_upload", symfile, *url).CombinedOutput()
	if err != nil {
		log.Fatal("sym_upload for ", symfile, " failed with output ", string(out), " error ", err)
	}
}

func mkdir(d string) {
	if *verbose || *dryRun {
		fmt.Println("Making directory", d)
	}
	if *dryRun {
		return
	}
	_, err := exec.Command("mkdir", "-p", d).Output()
	if err != nil {
		log.Fatal("could not create directory", d)
	}
}

// Based on https://github.com/xtgo/set/blob/master/mutators.go#L17.
func uniq(data sort.StringSlice) sort.StringSlice {
	p, l := 0, data.Len()
	if l <= 1 {
		return data
	}
	for i := 1; i < l; i++ {
		if !data.Less(p, i) {
			continue
		}
		p++
		if p < i {
			data.Swap(p, i)
		}
	}
	return data[:p+1]
}

func main() {
	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, `Usage ./upload-symbols.go [flags] /path/to/fuchsia/root

This script converts the symbols for a built tree into a format suitable for the
crash server and then optionally uploads them.
`)
		flag.PrintDefaults()
	}

	flag.Parse()

	fuchsiaRoot := flag.Arg(0)
	if _, err := os.Stat(fuchsiaRoot); os.IsNotExist(err) {
		flag.Usage()
		log.Fatalf("Fuchsia root not found at \"%v\"\n", fuchsiaRoot)
	}
	cwd, err := os.Getwd()
	if err != nil {
		log.Fatalf("Could not Getwd")
	}
	cwd, err = filepath.EvalSymlinks(cwd)
	if err != nil {
		log.Fatalf("Could not EvalSymlinks")
	}
	fuchsiaRoot = filepath.Join(cwd, fuchsiaRoot)

	if *symbolDir == "" {
		var err error
		*symbolDir, err = ioutil.TempDir("", "crash-symbols")
		if err != nil {
			log.Fatal("Could not create temporary directory: ", err)
		}
		defer os.RemoveAll(*symbolDir)
	} else if _, err := os.Stat(*symbolDir); os.IsNotExist(err) {
		mkdir(*symbolDir)
	}

	var binaries sort.StringSlice
	zxBuildDir := "out/build-zircon"
	if *x64Symbols {
		x64BuildDir := "out/release-x64"
		x64ZxBuildDir := path.Join(zxBuildDir, "build-x64")
		binaries = append(binaries, getBinariesFromIds(path.Join(fuchsiaRoot, x64BuildDir, "ids.txt"))...)
		binaries = append(binaries, getBinariesFromIds(path.Join(fuchsiaRoot, x64ZxBuildDir, "ids.txt"))...)
	}
	if *armSymbols {
		armBuildDir := "out/release-arm64"
		armZxBuildDir := path.Join(zxBuildDir, "build-arm64")
		binaries = append(binaries, getBinariesFromIds(path.Join(fuchsiaRoot, armBuildDir, "ids.txt"))...)
		binaries = append(binaries, getBinariesFromIds(path.Join(fuchsiaRoot, armZxBuildDir, "ids.txt"))...)
	}
	sort.Sort(binaries)
	binaries = uniq(binaries)
	for i, v := range binaries {
		binaries[i], err = filepath.EvalSymlinks(v)
		if err != nil {
			log.Fatalf("Could not EvalSymlinks")
		}
	}

	sem := make(chan bool, len(binaries))
	for _, binary := range binaries {
		go func(binary string) {
			symfile := dump(binary, fuchsiaRoot)
			if *upload {
				uploadSymbols(symfile)
			}
			sem <- true
		}(binary)
	}
	for i := 0; i < len(binaries); i++ {
		<-sem
	}
}
