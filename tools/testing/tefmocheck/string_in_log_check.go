// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tefmocheck

import (
	"bytes"
	"fmt"
	"path"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/bootserver/bootserverconstants"
	botanistconstants "go.fuchsia.dev/fuchsia/tools/botanist/constants"
	netutilconstants "go.fuchsia.dev/fuchsia/tools/net/netutil/constants"
	sshutilconstants "go.fuchsia.dev/fuchsia/tools/net/sshutil/constants"
	testrunnerconstants "go.fuchsia.dev/fuchsia/tools/testing/testrunner/constants"
)

// stringInLogCheck checks if String is found in the log named LogName.
type stringInLogCheck struct {
	String string
	// ExceptString will cause Check() to return false if present.
	ExceptString string
	// ExceptBlocks will cause Check() to return false if the string is only within these blocks.
	ExceptBlocks   []*logBlock
	Type           logType
	swarmingResult *SwarmingRpcsTaskResult
}

func (c *stringInLogCheck) Check(to *TestingOutputs) bool {
	c.swarmingResult = to.SwarmingSummary.Results
	var toCheck []byte
	switch c.Type {
	case serialLogType:
		toCheck = to.SerialLog
	case swarmingOutputType:
		toCheck = to.SwarmingOutput
	case syslogType:
		toCheck = to.Syslog
	}
	if c.ExceptString != "" && bytes.Contains(toCheck, []byte(c.ExceptString)) {
		return false
	}
	stringBytes := []byte(c.String)
	if len(c.ExceptBlocks) == 0 {
		return bytes.Contains(toCheck, stringBytes)
	}
	index := bytes.Index(toCheck, stringBytes)
	for index >= 0 {
		foundString := true
		beforeBlock := toCheck[:index]
		nextStartIndex := index + len(stringBytes)
		if nextStartIndex > len(toCheck) {
			// The string was found at the end of the log, so it won't be included in
			// any exceptBlocks.
			return true
		}
		afterBlock := toCheck[nextStartIndex:]
		for _, block := range c.ExceptBlocks {
			closestStartIndex := bytes.LastIndex(beforeBlock, []byte(block.startString))
			if closestStartIndex < 0 {
				// There is no start string before this occurrence, so it must not be
				// included in this exceptBlock. Check the next exceptBlock.
				continue
			}
			closestEndIndex := bytes.LastIndex(beforeBlock, []byte(block.endString))
			if closestEndIndex < closestStartIndex {
				// There is no end string between the start string and the string to
				// check, so check if end string appears after. If so, then this
				// occurrence is included in this exceptBlock, so we can break and
				// check the next occurrence of the string.
				if bytes.Index(afterBlock, []byte(block.endString)) >= 0 {
					foundString = false
					break
				}
			}
		}
		if foundString {
			return true
		}
		index = bytes.Index(afterBlock, stringBytes)
		if index >= 0 {
			index += nextStartIndex
		}
	}
	return false
}

func (c *stringInLogCheck) Name() string {
	return path.Join("string_in_log", string(c.Type), strings.ReplaceAll(c.String, " ", "_"))
}

func (c *stringInLogCheck) DebugText() string {
	debugStr := fmt.Sprintf("Found the string \"%s\" in %s for task %s.", c.String, c.Type, c.swarmingResult.TaskId)
	debugStr += "\nThat file should be accessible from the build result page or Sponge.\n"
	if c.ExceptString != "" {
		debugStr += fmt.Sprintf("\nDid not find the exception string \"%s\"", c.ExceptString)
	}
	if len(c.ExceptBlocks) > 0 {
		for _, block := range c.ExceptBlocks {
			debugStr += fmt.Sprintf("\nDid not occur inside a block delimited by:\nSTART: %s\nEND: %s", block.startString, block.endString)
		}
	}
	return debugStr
}

func driverHostCrash(hostName, exceptHost string) *stringInLogCheck {
	c := stringInLogCheck{String: "<== fatal : process driver_host:" + hostName, Type: serialLogType}
	if exceptHost != "" {
		c.ExceptString = "<== fatal : process driver_host:" + exceptHost
	}
	return &c
}

// StringInLogsChecks returns checks to detect bad strings in certain logs.
func StringInLogsChecks() (ret []FailureModeCheck) {
	// For fxbug.dev/57548.
	// Hardware watchdog tripped, should not happen.
	// This string is specified in u-boot.
	ret = append(ret, &stringInLogCheck{String: "reboot_mode=watchdog_reboot", Type: serialLogType})
	// For fxbug.dev/47649.
	ret = append(ret, &stringInLogCheck{String: "kvm run failed Bad address", Type: swarmingOutputType})
	// For fxbug.dev/44779.
	ret = append(ret, &stringInLogCheck{String: netutilconstants.CannotFindNodeErrMsg, Type: swarmingOutputType})
	// For fxbug.dev/51015.
	ret = append(ret, &stringInLogCheck{String: bootserverconstants.FailedToSendErrMsg(bootserverconstants.CmdlineNetsvcName), Type: swarmingOutputType})
	// For fxbug.dev/43188.
	ret = append(ret, &stringInLogCheck{String: "/dev/net/tun (qemu): Device or resource busy", Type: swarmingOutputType})
	// For fxbug.dev/57463.
	ret = append(ret, &stringInLogCheck{String: fmt.Sprintf("botanist ERROR: %s: signal: segmentation fault", botanistconstants.QEMUInvocationErrorMsg), Type: swarmingOutputType})
	// For fxbug.dev/43355.
	ret = append(ret, &stringInLogCheck{String: "Timed out loading dynamic linker from fuchsia.ldsvc.Loader", Type: swarmingOutputType})
	// For fxbug.dev/53854.
	ret = append(ret, driverHostCrash("composite-device", ""))
	ret = append(ret, driverHostCrash("pci", ""))
	// Don't fail if we see PDEV_DID_CRASH_TEST, defined in
	// zircon/system/ulib/ddk-platform-defs/include/ddk/platform-defs.h.
	// That's used for a test that intentionally crashes a driver host.
	ret = append(ret, driverHostCrash("pdev", "pdev:00:00:24"))
	// Catch-all for driver host crashes.
	ret = append(ret, driverHostCrash("", "pdev:00:00:24"))
	// These are rather generic. New checks should probably go above here so that they run before these.
	allLogTypes := []logType{serialLogType, swarmingOutputType, syslogType}
	for _, lt := range allLogTypes {
		ret = append(ret, &stringInLogCheck{String: "ERROR: AddressSanitizer", Type: lt})
		ret = append(ret, &stringInLogCheck{String: "ERROR: LeakSanitizer", Type: lt, ExceptBlocks: []*logBlock{
			// Kernel out-of-memory test "OOMHard" may report false positive leaks.
			{startString: "RUN   TestOOMHard", endString: "PASS: TestOOMHard"},
		}})
		ret = append(ret, &stringInLogCheck{String: "SUMMARY: UndefinedBehaviorSanitizer", Type: lt})
		ret = append(ret, &stringInLogCheck{
			String: "ZIRCON KERNEL OOPS",
			Type:   lt,
			ExceptBlocks: []*logBlock{
				{startString: " lock_dep_dynamic_analysis_tests ", endString: " lock_dep_static_analysis_tests "},
				{startString: "RUN   TestKillCriticalProcess", endString: ": TestKillCriticalProcess"},
			},
		})
		ret = append(ret, &stringInLogCheck{String: "ZIRCON KERNEL PANIC", Type: lt})
	}
	// These may be in the output of tests, but the syslogType doesn't contain any test output.
	ret = append(ret, &stringInLogCheck{String: "ASSERT FAILED", Type: syslogType})
	ret = append(ret, &stringInLogCheck{String: "DEVICE SUSPEND TIMED OUT", Type: syslogType})

	// For fxbug.dev/53101.
	ret = append(ret, &stringInLogCheck{String: fmt.Sprintf("botanist ERROR: %s", botanistconstants.FailedToStartTargetMsg), Type: swarmingOutputType})
	// For fxbug.dev/51441.
	ret = append(ret, &stringInLogCheck{String: fmt.Sprintf("botanist ERROR: %s", botanistconstants.ReadConfigFileErrorMsg), Type: swarmingOutputType})
	// For fxbug.dev/56494.
	ret = append(ret, &stringInLogCheck{String: fmt.Sprintf("botanist ERROR: %s", botanistconstants.FailedToReceiveFileMsg), Type: swarmingOutputType})
	// For fxbug.dev/59237.
	ret = append(ret, &stringInLogCheck{String: fmt.Sprintf("botanist ERROR: %s", sshutilconstants.TimedOutConnectingMsg), Type: swarmingOutputType})
	// For fxbug.dev/56651.
	ret = append(ret, &stringInLogCheck{String: fmt.Sprintf("testrunner ERROR: %s", testrunnerconstants.FailedToRunSnapshotMsg), Type: swarmingOutputType})
	// For fxbug.dev/52719.
	// Kernel panics and other low-level errors often cause crashes that
	// manifest as SSH failures, so this check must come after all
	// Zircon-related errors to ensure tefmocheck attributes these crashes to
	// the actual root cause.
	ret = append(ret, &stringInLogCheck{String: fmt.Sprintf("testrunner ERROR: %s", testrunnerconstants.FailedToReconnectMsg), Type: swarmingOutputType})
	ret = append(ret, &stringInLogCheck{String: "failed to resolve fuchsia-pkg://fuchsia.com/run_test_component#bin/run-test-component", Type: swarmingOutputType})
	return ret
}
