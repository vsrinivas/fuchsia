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
	Name     string
	Status   TestCaseStatus
	Duration time.Duration
}

func makeTestCaseResult(name []byte, status TestCaseStatus, duration []byte) TestCaseResult {
	durationString := strings.ReplaceAll(string(duration), " ", "")
	parsedDuration, _ := time.ParseDuration(durationString)
	return TestCaseResult{string(name), status, parsedDuration}
}
