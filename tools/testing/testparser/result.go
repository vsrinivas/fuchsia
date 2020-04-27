// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testparser

import (
	"strings"
	"time"
)

type TestCaseStatus string

const (
	Pass TestCaseStatus = "Pass"
	Fail TestCaseStatus = "Fail"
	Skip TestCaseStatus = "Skip"
)

type TestCaseResult struct {
	// Fully qualified test case name
	Name     string         `json:"name"`
	Status   TestCaseStatus `json:"status"`
	Duration time.Duration  `json:"duration_nanos"`
	// Identified result format, e.g. "GoogleTest"
	Format   string         `json:"format"`
}

func makeTestCaseResult(name []byte, status TestCaseStatus, duration []byte, format string) TestCaseResult {
	// Normalize e.g. "4 ms" to "4ms" before calling ParseDuration
	durationString := strings.ReplaceAll(string(duration), " ", "")
	parsedDuration, _ := time.ParseDuration(durationString)
	return TestCaseResult{string(name), status, parsedDuration, format}
}
