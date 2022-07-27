// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ffxutil

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"
	"time"

	"go.fuchsia.dev/fuchsia/tools/build"
)

type MockFFXInstance struct {
	CmdsCalled  []string
	TestOutcome string
}

func (f *MockFFXInstance) SetStdoutStderr(_, _ io.Writer) {
}

func (f *MockFFXInstance) run(cmd string, args ...string) error {
	f.CmdsCalled = append(f.CmdsCalled, fmt.Sprintf("%s:%s", cmd, strings.Join(args, " ")))
	return nil
}

func (f *MockFFXInstance) Test(_ context.Context, testList build.TestList, outDir string, args ...string) (*TestRunResult, error) {
	if testList.SchemaID != build.TestListSchemaIDExperimental {
		return nil, fmt.Errorf(`schema_id must be %q, found %q`, build.TestListSchemaIDExperimental, testList.SchemaID)
	}
	f.run("test", args...)
	outcome := TestPassed
	if f.TestOutcome != "" {
		outcome = f.TestOutcome
	}
	if err := os.MkdirAll(outDir, os.ModePerm); err != nil {
		return nil, err
	}
	var suites []suiteEntry
	for i, test := range testList.Data {
		relTestDir := fmt.Sprintf("test%d", i)
		if err := os.Mkdir(filepath.Join(outDir, relTestDir), os.ModePerm); err != nil {
			return nil, err
		}
		if err := ioutil.WriteFile(filepath.Join(outDir, relTestDir, "report.txt"), []byte("stdio"), os.ModePerm); err != nil {
			return nil, err
		}
		suite := suiteEntry{fmt.Sprintf("summary%d.json", i)}
		summaryFile := filepath.Join(outDir, suite.Summary)
		fmt.Println("writing to ", summaryFile)
		summaryBytes, err := json.Marshal(SuiteResult{
			Outcome: outcome,
			Name:    test.Execution.ComponentURL,
			Cases: []CaseResult{
				{
					Outcome:              outcome,
					Name:                 "case1",
					StartTime:            time.Now().UnixMilli(),
					DurationMilliseconds: 1000,
				},
			},
			StartTime:            time.Now().UnixMilli(),
			DurationMilliseconds: 1000,
			Artifacts: map[string]ArtifactMetadata{
				"report.txt": {
					ArtifactType: ReportType,
				}},
			ArtifactDir: relTestDir,
		})
		if err != nil {
			return nil, err
		}
		if err := ioutil.WriteFile(summaryFile, summaryBytes, os.ModePerm); err != nil {
			return nil, err
		}
		suites = append(suites, suite)
	}
	runArtifactDir := "artifact-run"
	debugDir := "debug"
	if err := os.MkdirAll(filepath.Join(outDir, runArtifactDir, debugDir), os.ModePerm); err != nil {
		return nil, err
	}
	if err := ioutil.WriteFile(filepath.Join(outDir, runArtifactDir, debugDir, "kernel.profraw"), []byte("data"), os.ModePerm); err != nil {
		return nil, err
	}

	runResult := &TestRunResult{
		Artifacts: map[string]ArtifactMetadata{
			debugDir: {
				ArtifactType: DebugType,
			},
		},
		ArtifactDir: runArtifactDir,
		Outcome:     outcome,
		Suites:      suites,
		outputDir:   outDir,
	}
	runResultBytes, err := json.Marshal(*runResult)
	if err != nil {
		return nil, err
	}
	if err := ioutil.WriteFile(filepath.Join(outDir, runSummaryFilename), runResultBytes, os.ModePerm); err != nil {
		return nil, err
	}
	return runResult, nil
}

func (f *MockFFXInstance) Snapshot(_ context.Context, _, _ string) error {
	return f.run("snapshot")
}

func (f *MockFFXInstance) Stop() error {
	return f.run("stop")
}

func (f *MockFFXInstance) ContainsCmd(cmd string, args ...string) bool {
	for _, c := range f.CmdsCalled {
		parts := strings.Split(c, ":")
		if parts[0] == cmd {
			for _, arg := range args {
				if !strings.Contains(parts[1], arg) {
					return false
				}
			}
			return true
		}
	}
	return false
}
