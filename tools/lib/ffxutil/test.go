// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ffxutil

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
)

const (
	// The name of the summary.json describing a test run.
	runSummaryFilename = "run_summary.json"
	// ID of the schema understood by ffxutil.
	runSummarySchemaID = "https://fuchsia.dev/schema/ffx_test/run_summary-8d1dd964.json"

	// Test outcome values. This should match the list of values for the `outcome` field
	// of the run summary at //src/sys/run_test_suite/directory/schema/suite_summary.schema.json
	TestNotStarted   = "NOT_STARTED"
	TestPassed       = "PASSED"
	TestFailed       = "FAILED"
	TestInconclusive = "INCONCLUSIVE"
	TestTimedOut     = "TIMEDOUT"
	TestError        = "ERROR"
	TestSkipped      = "SKIPPED"

	// Artifact types. This should match the list under `artifact_metadata.artifact_type`
	// at //src/sys/run_test_suite/directory/schema/suite_summary.schema.json.
	SyslogType        = "SYSLOG"
	StdoutType        = "STDOUT"
	StderrType        = "STDERR"
	CustomType        = "CUSTOM"
	RestrictedLogType = "RESTRICTED_LOG"
	ReportType        = "REPORT"
	DebugType         = "DEBUG"
)

// TestRunResultEnvelope is the JSON schema for the versioned envelope that contains
// test run results.
type TestRunResultEnvelope struct {
	Data     TestRunResult `json:"data"`
	SchemaID string        `json:"schema_id"`
}

// TestRunResult is the JSON schema for a test run in structured results output
// by `ffx test run`.
type TestRunResult struct {
	Artifacts   map[string]ArtifactMetadata `json:"artifacts"`
	ArtifactDir string                      `json:"artifact_dir"`
	Outcome     string                      `json:"outcome"`
	Suites      []SuiteResult               `json:"suites"`
	outputDir   string
}

func GetRunResult(outputDir string) (*TestRunResult, error) {
	runSummaryBytes, err := os.ReadFile(filepath.Join(outputDir, runSummaryFilename))
	if err != nil {
		return nil, err
	}
	runResultEnvelope := &TestRunResultEnvelope{}
	err = json.Unmarshal(runSummaryBytes, runResultEnvelope)
	if err != nil {
		return nil, err
	}
	if runResultEnvelope.SchemaID != runSummarySchemaID {
		return nil, fmt.Errorf("unrecognized schema: %s", runResultEnvelope.SchemaID)
	}
	runResultEnvelope.Data.outputDir = outputDir
	return &runResultEnvelope.Data, nil
}

// GetSuiteResults returns a list of the suite summaries from a test run.
func (r *TestRunResult) GetSuiteResults() ([]SuiteResult, error) {
	return r.Suites, nil
}

// GetTestOutputDir returns the path to the test output directory.
func (r *TestRunResult) GetTestOutputDir() string {
	return r.outputDir
}

// SuiteResult is a JSON schema for a suite in structured results output by `ffx test run`.
type SuiteResult struct {
	Outcome              string                      `json:"outcome"`
	Name                 string                      `json:"name"`
	Cases                []CaseResult                `json:"cases"`
	StartTime            int64                       `json:"start_time"`
	DurationMilliseconds int64                       `json:"duration_milliseconds"`
	Artifacts            map[string]ArtifactMetadata `json:"artifacts"`
	ArtifactDir          string                      `json:"artifact_dir"`
}

// ArtifactMetadata is metadata tied to an artifact.
type ArtifactMetadata struct {
	ArtifactType     string `json:"artifact_type"`
	ComponentMoniker string `json:"component_moniker"`
}

// CaseResult is a JSON schema for a test case in structured results.
type CaseResult struct {
	Outcome              string                      `json:"outcome"`
	Name                 string                      `json:"name"`
	StartTime            int64                       `json:"start_time"`
	DurationMilliseconds int64                       `json:"duration_milliseconds"`
	Artifacts            map[string]ArtifactMetadata `json:"artifacts"`
	ArtifactDir          string                      `json:"artifact_dir"`
}
