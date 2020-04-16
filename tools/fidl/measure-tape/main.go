// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"flag"
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"regexp"
	"strings"

	fidlir "fidl/compiler/backend/types"

	"measure-tape/measurer"
)

type paths []string

func (filenames *paths) String() string {
	return strings.Join(*filenames, " ")
}

func (filenames *paths) Set(filename string) error {
	*filenames = append(*filenames, filename)
	return nil
}

var jsonFiles paths
var targetType = flag.String("target-type", "",
	"Target type to measure, e.g. fuchsia.ui.scenic/Command")
var outCc = flag.String("out-cc", "",
	"Write path for .cc file")
var outH = flag.String("out-h", "",
	"Write path for .h file")
var hIncludePath = flag.String("h-include-path", "",
	"Include path for the .h file")
var onlyCheckToFile = flag.String("only-check-to-file", "",
	"Enables verification only mode, which checks the .cc and .h")

func flagsValid() bool {
	if len(jsonFiles) == 0 {
		return false
	}
	if len(*targetType) == 0 {
		return false
	}
	if len(*outCc) == 0 {
		return false
	}
	if len(*outH) == 0 {
		return false
	}
	if len(*hIncludePath) == 0 {
		return false
	}
	return true
}

func main() {
	flag.Var(&jsonFiles, "json", "Path(s) to JSON IR")
	flag.Parse()

	if !flag.Parsed() || !flagsValid() {
		flag.PrintDefaults()
		os.Exit(1)
	}

	var roots []fidlir.Root
	for _, filename := range jsonFiles {
		root, err := fidlir.ReadJSONIr(filename)
		if err != nil {
			log.Fatal(err)
		}
		roots = append(roots, root)
	}

	m := measurer.NewMeasurer(roots)
	mt, err := m.MeasuringTapeFor(*targetType)
	if err != nil {
		log.Panic(err.Error())
	}

	var bufH, bufCc bytes.Buffer
	{
		printer := measurer.NewPrinter(m, mt, *hIncludePath, &bufH)
		printer.WriteH()
	}
	{
		printer := measurer.NewPrinter(m, mt, *hIncludePath, &bufCc)
		printer.WriteCc()
	}

	if len(*onlyCheckToFile) == 0 {
		writeFile(*outH, bufH.Bytes())
		writeFile(*outCc, bufCc.Bytes())
	} else {
		verifyMeasureTape(bufH.Bytes(), bufCc.Bytes())
	}
}

func writeFile(path string, data []byte) {
	if err := ioutil.WriteFile(path, data, 0644); err != nil {
		log.Panic(err.Error())
	}
}

func verifyMeasureTape(expectedH, expectedCc []byte) {
	actualH, err := ioutil.ReadFile(*outH)
	if err != nil {
		log.Panic(err.Error())
	}
	actualCc, err := ioutil.ReadFile(*outCc)
	if err != nil {
		log.Panic(err.Error())
	}
	if bytes.Compare(actualH, expectedH) != 0 || bytes.Compare(actualCc, expectedCc) != 0 {
		fmt.Fprintf(os.Stderr, "%s and/or %s is out of date! Please run the following\n\n", *outH, *outCc)
		skipUntil := 0
		for i, arg := range os.Args {
			if matched, _ := regexp.MatchString("^-?-only-check-to-file$", arg); matched {
				skipUntil = i + 2
				continue
			}
			if i < skipUntil {
				continue
			}
			if i != 0 {
				fmt.Fprintf(os.Stderr, " \\\n\t")
			}
			fmt.Fprintf(os.Stderr, "%s", arg)
		}
		fmt.Fprintf(os.Stderr, "\n\n")
		os.Exit(1)
	}
}
