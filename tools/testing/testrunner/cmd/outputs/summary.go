// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package outputs

import (
	"path"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
	"go.fuchsia.dev/fuchsia/tools/testing/testrunner/lib"
)

// SummaryOutput records test results in a TestSummary object.
type SummaryOutput struct {
	Summary runtests.TestSummary
}

// Record writes a TestDetails entry for the given test into a Summary.
func (o *SummaryOutput) Record(result testrunner.TestResult) {
	pathInArchive := path.Join(result.Name, runtests.TestOutputFilename)
	// Strip any leading //, contributed by Linux/Mac test names, so that
	// pathInArchive gives a valid relative path.
	pathInArchive = strings.TrimLeft(pathInArchive, "//")
	o.Summary.Tests = append(o.Summary.Tests, runtests.TestDetails{
		Name:           result.Name,
		GNLabel:        result.GNLabel,
		OutputFile:     pathInArchive,
		Result:         result.Result,
		DurationMillis: result.EndTime.Sub(result.StartTime).Nanoseconds() / 1000 / 1000,
		DataSinks:      result.DataSinks,
	})
}

// AddFile registers a file on disk as a file to include in the summary.
func (o *SummaryOutput) AddFile(name, path string) {
	if o.Summary.Outputs == nil {
		o.Summary.Outputs = make(map[string]string)
	}
	o.Summary.Outputs[name] = path
}
