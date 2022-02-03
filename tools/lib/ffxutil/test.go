// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ffxutil

import (
	"encoding/json"
	"os"
	"path/filepath"
)

const (
	// The name of the summary.json describing a test run.
	runSummaryFilename = "run_summary.json"

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
)

// TestDef is the JSON schema for input to `ffx test run`.
// Note this only contains the subset of options that are currently used.
type TestDef struct {
	TestUrl         string `json:"test_url"`
	Timeout         int    `json:"timeout,omitempty"`
	Parallel        uint16 `json:"parallel,omitempty"`
	MaxSeverityLogs string `json:"max_severity_logs,omitempty"`
}

// TestRunResult is the JSON schema for a test run in structured results output
// by `ffx test run`.
type TestRunResult struct {
	Outcome   string       `json:"outcome"`
	Suites    []suiteEntry `json:"suites"`
	outputDir string
}

func getRunResult(outputDir string) (*TestRunResult, error) {
	runSummaryBytes, err := os.ReadFile(filepath.Join(outputDir, runSummaryFilename))
	if err != nil {
		return nil, err
	}
	runResult := &TestRunResult{}
	err = json.Unmarshal(runSummaryBytes, runResult)
	if err != nil {
		return nil, err
	}
	runResult.outputDir = outputDir
	return runResult, nil
}

// GetSuiteResults returns a list of the suite summaries from a test run.
func (r *TestRunResult) GetSuiteResults() ([]*SuiteResult, error) {
	var suiteResults []*SuiteResult
	for _, suite := range r.Suites {
		suiteSummaryBytes, err := os.ReadFile(filepath.Join(r.outputDir, suite.Summary))
		if err != nil {
			return suiteResults, err
		}
		suiteResult := &SuiteResult{}
		err = json.Unmarshal(suiteSummaryBytes, suiteResult)
		if err != nil {
			return suiteResults, err
		}
		suiteResults = append(suiteResults, suiteResult)
	}
	return suiteResults, nil
}

// GetTestOutputDir returns the path to the test output directory.
func (r *TestRunResult) GetTestOutputDir() string {
	return r.outputDir
}

// suiteEntry is an entry for a test suite in TestRunResult.
type suiteEntry struct {
	Summary string `json:"summary"`
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
