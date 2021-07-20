// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"encoding/json"
	"flag"
	"fmt"
	"io/ioutil"
	"os"

	"go.fuchsia.dev/fuchsia/tools/testing/testparser"
)

func usage() {
	fmt.Printf(`testparser [-file <path>]

Reads test logs from either <path> or stdin, and writes a JSON formatted summary to stdout
of any error messages parsed from the logs.
`)
}

func indentJSON(jsonBytes []byte) ([]byte, error) {
	buffer := bytes.NewBuffer([]byte{})
	err := json.Indent(buffer, jsonBytes, "", "\t")
	return buffer.Bytes(), err
}

func main() {
	inputPath := flag.String("file", "", "Path to a file to be parsed. Optional; defaults to stdin.")
	flag.Usage = usage

	// Parse any global flags (e.g. those for glog)
	flag.Parse()

	var inputBytes []byte
	var err error
	if *inputPath != "" {
		inputBytes, err = ioutil.ReadFile(*inputPath)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error reading input path %s: %s\n", *inputPath, err)
			os.Exit(1)
		}
	} else {
		inputBytes, err = ioutil.ReadAll(os.Stdin)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error reading input: %s\n", err)
			os.Exit(1)
		}
	}

	result := testparser.Parse(inputBytes)
	jsonData, err := json.Marshal(result)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error marshaling JSON: %s\n", err)
		os.Exit(1)
	}
	indentedData, err := indentJSON(jsonData)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error indenting JSON: %s\n", err)
		os.Exit(1)
	}
	os.Stdout.Write(indentedData)
}
