// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package outputs

import (
	"fmt"
	"io"

	"go.fuchsia.dev/tools/runtests"
	"go.fuchsia.dev/tools/tap"
	"go.fuchsia.dev/tools/testrunner"
)

// TAPOutput records test results as a TAP output stream.
type TAPOutput struct {
	producer *tap.Producer
}

// NewTAPOutput creates a new TAPOutput that streams to the given writer. testCount is
// used to print the TAP plan line.
func NewTAPOutput(output io.Writer, testCount int) *TAPOutput {
	producer := tap.NewProducer(output)
	producer.Plan(testCount)
	return &TAPOutput{producer}
}

// Record writes the test's result and name to the output given at construction time.
func (o *TAPOutput) Record(result testrunner.TestResult) {
	desc := fmt.Sprintf("%s (%v)", result.Name, result.EndTime.Sub(result.StartTime))
	o.producer.Ok(result.Result == runtests.TestSuccess, desc)
}
