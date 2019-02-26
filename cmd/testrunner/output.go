// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"encoding/json"
	"fmt"
	"io"
	"os"

	"fuchsia.googlesource.com/tools/cmd/testrunner/outputs"
	"fuchsia.googlesource.com/tools/runtests"
	"fuchsia.googlesource.com/tools/testrunner"
)

// Output manages the test runner's output drivers. Upon completion, if tar output is
// initialized, a TAR archive containing all other outputs is produced.
type Output struct {
	Summary *outputs.SummaryOutput
	TAP     *outputs.TAPOutput
	Tar     *outputs.TarOutput
}

// SetupTAP intializes a TAPOutput stream. out is where the stream is written. testCount
// is used to print the TAP plan (e.g. 1..testCount). See outputs.TAPOutput for full docs.
func (o *Output) SetupTAP(out io.Writer, testCount int) {
	o.TAP = outputs.NewTAPOutput(out, testCount)
}

// SetupSummary initializes a Test Summary output. See outputs.SummaryOutput for docs.
func (o *Output) SetupSummary() {
	o.Summary = &outputs.SummaryOutput{}
}

// SetupTar initializes a TarOutput. See outputs.TarOutput for docs.
func (o *Output) SetupTar(archivePath string) error {
	fd, err := os.Create(archivePath)
	if err != nil {
		return fmt.Errorf("failed to open %q: %v", archivePath, err)
	}
	o.Tar = outputs.NewTarOutput(fd)
	return nil
}

// Record writes the test result to initialized outputs.
func (o *Output) Record(result testrunner.TestResult) {
	if o.Summary != nil {
		o.Summary.Record(result)
	}
	if o.TAP != nil {
		o.TAP.Record(result)
	}
	if o.Tar != nil {
		o.Tar.Record(result)
	}
}

// Complete finishes producing output for the test run.
func (o *Output) Complete() error {
	if o.Tar == nil {
		return nil
	}
	bytes, err := json.Marshal(o.Summary.Summary)
	if err != nil {
		return err
	}
	if err := o.Tar.TarFile(bytes, runtests.TestSummaryFilename); err != nil {
		return err
	}
	return o.Tar.Close()
}
