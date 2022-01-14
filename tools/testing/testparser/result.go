// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testparser

import (
	"time"
)

type TestCaseStatus string

const (
	Pass  TestCaseStatus = "Pass"
	Fail  TestCaseStatus = "Fail"
	Skip  TestCaseStatus = "Skip"
	Abort TestCaseStatus = "Abort"
)

type TestCaseResult struct {
	DisplayName string         `json:"display_name"`
	SuiteName   string         `json:"suite_name"`
	CaseName    string         `json:"case_name"`
	Status      TestCaseStatus `json:"status"`
	Duration    time.Duration  `json:"duration_nanos"`
	// Format is the test runner used to execute the test.
	Format string `json:"format"`
	// FailReason is a concise and distinctive error message captured from stdout when the test case fails.
	// The message is used to group similar failures and shouldn't contain stacktrace or line numbers.
	FailReason  string   `json:"fail_reason"`
	OutputFiles []string `json:"output_files,omitempty"`
	// The directory where the OutputFiles live if given as relative paths.
	OutputDir string `json:"output_dir,omitempty"`
}
