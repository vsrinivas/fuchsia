// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testparser

import (
	"time"
)

type TestCaseStatus string

const (
	Pass TestCaseStatus = "Pass"
	Fail TestCaseStatus = "Fail"
	Skip TestCaseStatus = "Skip"
)

type TestCaseResult struct {
	DisplayName string         `json:"display_name"`
	SuiteName   string         `json:"suite_name"`
	CaseName    string         `json:"case_name"`
	Status      TestCaseStatus `json:"status"`
	Duration    time.Duration  `json:"duration_nanos"`
	Format      string         `json:"format"`
}
