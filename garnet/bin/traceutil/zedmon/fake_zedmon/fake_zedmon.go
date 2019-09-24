// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"io/ioutil"
	"log"
	"os"
	"os/signal"
	"path/filepath"
)

// This program writes predefined data to stderr and stdout. It expects to find
// the predefined data in two files given by its own path plus the suffixes
// ".stderr.testdata" and ".stdout.testdata".
//
// It will terminate when it has both written its data and received a SIGINT.
func main() {
	// Set a trap that prevents exit until SIGINT is received and we're done writing data.
	sigint := make(chan os.Signal)
	signal.Notify(sigint, os.Interrupt)
	done := make(chan bool)
	go func() {
		<-sigint
		<-done
	}()

	myPath, err := filepath.Abs(os.Args[0])
	stderrDataPath := myPath + ".stderr.testdata"
	stdoutDataPath := myPath + ".stdout.testdata"

	stderrData, err := ioutil.ReadFile(stderrDataPath)
	if err != nil {
		log.Fatalf("Failed to read file %s with error %v", stderrDataPath, err)
	}
	stdoutData, err := ioutil.ReadFile(stdoutDataPath)
	if err != nil {
		log.Fatalf("Failed to read file %s with error %v", stdoutDataPath, err)
	}

	os.Stderr.Write(stderrData)
	os.Stdout.Write(stdoutData)

	done <- true
}
