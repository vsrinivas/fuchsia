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
var typeToMeasure = flag.String("measure", "",
	"Target type to measure, e.g. fuchsia.ui.scenic/Command")
var outCc = flag.String("out-cc", "",
	"Write path for .cc file")
var onlyCheckToFile = flag.String("only-check-to-file", "",
	"Write path for .cc file")

func flagsValid() bool {
	if len(jsonFiles) == 0 {
		return false
	}
	if len(*typeToMeasure) == 0 {
		return false
	}
	if len(*outCc) == 0 {
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
	mt, err := m.MeasuringTapeFor(*typeToMeasure)
	if err != nil {
		log.Panic(err.Error())
	}

	var buf bytes.Buffer
	printer := measurer.NewPrinter(mt, &buf)
	printer.Write()

	if len(*onlyCheckToFile) == 0 {
		writeMeasureTape(buf.Bytes())
	} else {
		verifyMeasureTape(buf.Bytes())
	}
}

func writeMeasureTape(data []byte) {
	if err := ioutil.WriteFile(*outCc, data, 0644); err != nil {
		log.Panic(err.Error())
	}
}

func verifyMeasureTape(data []byte) {
	actual, err := ioutil.ReadFile(*outCc)
	if err != nil {
		log.Panic(err.Error())
	}
	if bytes.Compare(actual, data) != 0 {
		fmt.Fprintf(os.Stderr, "%s is out of date! Please run the following\n\n", *outCc)
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
