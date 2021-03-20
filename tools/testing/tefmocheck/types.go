// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tefmocheck

import "go.fuchsia.dev/fuchsia/tools/testing/runtests"

// TestingOutputs are the outputs of a testing Swarming task that are analyzed by a FailureModeCheck.
type TestingOutputs struct {
	TestSummary           *runtests.TestSummary
	SwarmingSummary       *SwarmingTaskSummary
	SerialLog             []byte
	SwarmingOutput        []byte
	SwarmingOutputPerTest []TestLog
	Syslog                []byte
}

type logType string

// It's not necessary for correctness, but it makes the output easier for users if these
// strings match the file names that the recipes use for these logs. See the link above
// each constant for the relevant recipes code.
const (
	// https://fuchsia.googlesource.com/infra/recipes/+/cac1bc9f13f9349228e917dfae26bb85b1a290a8/recipe_modules/testing_requests/api.py#32
	serialLogType logType = "serial_log.txt"
	// https://fuchsia.googlesource.com/infra/recipes/+/cac1bc9f13f9349228e917dfae26bb85b1a290a8/recipe_modules/testing/api.py#22
	swarmingOutputType logType = "infra_and_test_std_and_klog.txt"
	// https://fuchsia.googlesource.com/infra/recipes/+/cac1bc9f13f9349228e917dfae26bb85b1a290a8/recipe_modules/testing_requests/api.py#33
	syslogType logType = "syslog.txt"
)

type logBlock struct {
	startString string
	endString   string
}

// FailureModeCheck checks whether a failure mode appears.
type FailureModeCheck interface {
	// Check analyzes TestingOutputs and returns true if the failure mode was detected.
	Check(*TestingOutputs) bool
	// Name is the name of this check.
	Name() string
	// DebugText is human-readable text intended to help debug a check failure.
	DebugText() string
	// OutputFiles are paths associated with this check. Often empty.
	OutputFiles() []string
}
