// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tefmocheck

import (
	"bytes"
	"fmt"
	"path"
	"path/filepath"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/bootserver/bootserverconstants"
	botanistconstants "go.fuchsia.dev/fuchsia/tools/botanist/constants"
	ffxutilconstants "go.fuchsia.dev/fuchsia/tools/lib/ffxutil/constants"
	serialconstants "go.fuchsia.dev/fuchsia/tools/lib/serial/constants"
	syslogconstants "go.fuchsia.dev/fuchsia/tools/lib/syslog/constants"
	netutilconstants "go.fuchsia.dev/fuchsia/tools/net/netutil/constants"
	sshutilconstants "go.fuchsia.dev/fuchsia/tools/net/sshutil/constants"
	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
	testrunnerconstants "go.fuchsia.dev/fuchsia/tools/testing/testrunner/constants"
)

// stringInLogCheck checks if String is found in the log named LogName.
type stringInLogCheck struct {
	// String that will be searched for.
	String string
	// OnlyOnStates will cause Check() to return false if the swarming task
	// state doesn't match with one of these states.
	OnlyOnStates []string
	// ExceptString will cause Check() to return false if present.
	ExceptString string
	// ExceptBlocks will cause Check() to return false if the string is only
	// within these blocks. The start string and end string should be unique
	// strings that only appear around the except block. A stray start string
	// will cause everything after it to be included in the except block even
	// if the end string is missing.
	ExceptBlocks []*logBlock
	// SkipPassedTask will cause Check() to return false if the
	// Swarming task succeeded.
	SkipPassedTask bool
	// SkipAllPassedTests will cause Check() to return false if all tests
	// in the Swarming task passed.
	SkipAllPassedTests bool
	// Type of log that will be checked.
	Type logType
	// Whether to check the per-test Swarming output for this log and emit a
	// check that's specific to the test during which the log appeared.
	AttributeToTest bool

	swarmingResult *SwarmingRpcsTaskResult
	testName       string
	outputFile     string
}

func (c *stringInLogCheck) Check(to *TestingOutputs) bool {
	c.swarmingResult = to.SwarmingSummary.Results
	if !c.swarmingResult.Failure && c.swarmingResult.State == "COMPLETED" {
		if c.SkipPassedTask {
			return false
		}
		if c.SkipAllPassedTests {
			hasTestFailure := false
			for _, test := range to.TestSummary.Tests {
				if test.Result != runtests.TestSuccess {
					hasTestFailure = true
					break
				}
			}
			if !hasTestFailure {
				return false
			}
		}
	}
	matchedState := false
	for _, state := range c.OnlyOnStates {
		if c.swarmingResult.State == state {
			matchedState = true
			break
		}
	}
	if len(c.OnlyOnStates) != 0 && !matchedState {
		return false
	}

	if c.Type == swarmingOutputType && c.AttributeToTest {
		for _, testLog := range to.SwarmingOutputPerTest {
			if c.checkBytes(to.SwarmingOutput, testLog.Index, testLog.Index+len(testLog.Bytes)) {
				c.testName = testLog.TestName
				c.outputFile = testLog.FilePath
				return true
			}
		}
	}

	var toCheck [][]byte
	switch c.Type {
	case serialLogType:
		toCheck = to.SerialLogs
	case swarmingOutputType:
		toCheck = [][]byte{to.SwarmingOutput}
	case syslogType:
		toCheck = to.Syslogs
	}

	for _, file := range toCheck {
		if c.checkBytes(file, 0, len(file)) {
			return true
		}
	}
	return false
}

func (c *stringInLogCheck) checkBytes(toCheck []byte, start int, end int) bool {
	toCheckBlock := toCheck[start:end]
	if c.ExceptString != "" && bytes.Contains(toCheckBlock, []byte(c.ExceptString)) {
		return false
	}
	stringBytes := []byte(c.String)
	if len(c.ExceptBlocks) == 0 {
		return bytes.Contains(toCheckBlock, stringBytes)
	}
	index := bytes.Index(toCheckBlock, stringBytes) + start
	for index >= start && index < end {
		foundString := true
		beforeBlock := toCheck[:index]
		nextStartIndex := index + len(stringBytes)
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
				if bytes.Contains(afterBlock, []byte(block.endString)) {
					foundString = false
					break
				} else {
					// If the end string doesn't appear after the string to check,
					// it may have been truncated out of the log. In that case, we
					// assume every occurrence of the string to check between the
					// start string and the end of the block are included in the
					// exceptBlock.
					return false
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
	// TODO(fxbug.dev/71529): With multi-device logs, the file names may be different than
	// the log type. Consider using the actual filename of the log.
	return path.Join("string_in_log", string(c.Type), strings.ReplaceAll(c.String, " ", "_"), c.testName)
}

func (c *stringInLogCheck) DebugText() string {
	debugStr := fmt.Sprintf("Found the string \"%s\" in ", c.String)
	if c.outputFile != "" && c.testName != "" {
		debugStr += fmt.Sprintf("%s of test %s.", filepath.Base(c.outputFile), c.testName)
	} else {
		debugStr += fmt.Sprintf("%s for task %s.", c.Type, c.swarmingResult.TaskId)
	}
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

func (c *stringInLogCheck) OutputFiles() []string {
	if c.outputFile == "" {
		return []string{}
	}
	return []string{c.outputFile}
}

// StringInLogsChecks returns checks to detect bad strings in certain logs.
func StringInLogsChecks() []FailureModeCheck {
	ret := []FailureModeCheck{
		// For fxbug.dev/85875
		// This is printed by Swarming after a Swarming task's command completes, and
		// suggests that a test leaked a subprocess that modified one of the task's
		// output files after the task's command completed but before Swarming finished
		// uploading outputs.
		//
		// This is a serious issue and always causes the Swarming task to fail,
		// so we prioritize it over all other checks.
		&stringInLogCheck{String: "error: blob size changed while uploading", Type: swarmingOutputType},
		// Failure modes for CAS uploads from Swarming tasks during task cleanup
		// (outside the scope of the command run during the task). These logs
		// are unfortunately copy-pasted from the luci-go repository. These
		// failures are generally a result of a degradation in the upstream
		// RBE-CAS service.
		&stringInLogCheck{
			String:       "cas: failed to call UploadIfMissing",
			Type:         swarmingOutputType,
			OnlyOnStates: []string{"BOT_DIED"},
		},
		&stringInLogCheck{
			String:       "cas: failed to create cas client",
			Type:         swarmingOutputType,
			OnlyOnStates: []string{"BOT_DIED"},
		},
	}
	// Many of the infra tool checks match failure modes that have a root cause
	// somewhere within Fuchsia itself, so we want to make sure to check for
	// failures within the OS first to make sure we get as close to the root
	// cause as possible.
	ret = append(ret, fuchsiaLogChecks()...)
	ret = append(ret, infraToolLogChecks()...)
	return ret
}

// fuchsiaLogChecks returns checks for logs that come from the target Fuchsia
// device rather than from infrastructure host tools.
func fuchsiaLogChecks() []FailureModeCheck {
	ret := []FailureModeCheck{
		// For fxbug.dev/57548.
		// Hardware watchdog tripped, should not happen.
		// This string is specified in u-boot.
		// Astro uses an equal sign, Sherlock uses a colon. Consider allowing
		// regexes?
		// It is fine to have the two different checks because bug filing logic
		// already breaks down by device type.
		&stringInLogCheck{String: "reboot_mode=watchdog_reboot", Type: serialLogType},
		&stringInLogCheck{String: "reboot_mode:watchdog_reboot", Type: serialLogType},
		// For fxbug.dev/55637
		&stringInLogCheck{String: " in fx_logger::GetSeverity() ", Type: swarmingOutputType},
		// For fxbug.dev/71784. Do not check for this in swarming output as this does not indicate
		// an error if logged by unit tests.
		&stringInLogCheck{String: "intel-i915: No displays detected.", Type: serialLogType},
		&stringInLogCheck{String: "intel-i915: No displays detected.", Type: syslogType},
		// For fxbug.dev/105382 dwc2 bug that breaks usb cdc networking
		&stringInLogCheck{String: "diepint.timeout", Type: serialLogType, SkipAllPassedTests: true},
		// For devices which, typically as a result of wear, fail to read any copy of the
		// sys_config partition, give up on booting the intended slot, boot the R slot, and
		// severely confuse anyone who was expecting that to be something else.
		// Skip if the task passed; we aim to tolerate failing ECC until we are failing
		// tasks as a result.
		&stringInLogCheck{String: "sys_config: ERROR failed to read any copy", Type: serialLogType, SkipPassedTask: true},
		// Infra expects all devices to be locked when running tasks.  Certain requests may
		// be impossible to fulfill when the device is unlocked because they require secrets
		// which are only available when locked.  If a device is somehow running tasks while
		// unlocked, if the task attempts to make use of those secrets and can't, we'll see
		// this message in the log, and then an infra engineer should go re-lock the device.
		&stringInLogCheck{String: "Please re-lock the device", Type: serialLogType},
	}

	oopsExceptBlocks := []*logBlock{
		{startString: " lock_dep_dynamic_analysis_tests ", endString: " lock_dep_static_analysis_tests "},
		{startString: "RUN   TestKillCriticalProcess", endString: ": TestKillCriticalProcess"},
		{startString: "RUN   TestKernelLockupDetectorCriticalSection", endString: ": TestKernelLockupDetectorCriticalSection"},
		{startString: "RUN   TestKernelLockupDetectorHeartbeat", endString: ": TestKernelLockupDetectorHeartbeat"},
		{startString: "RUN   TestPmmCheckerOopsAndPanic", endString: ": TestPmmCheckerOopsAndPanic"},
		{startString: "RUN   TestKernelLockupDetectorFatalCriticalSection", endString: ": TestKernelLockupDetectorFatalCriticalSection"},
		{startString: "RUN   TestKernelLockupDetectorFatalHeartbeat", endString: ": TestKernelLockupDetectorFatalHeartbeat"},
	}
	// These are rather generic. New checks should probably go above here so that they run before these.
	allLogTypes := []logType{serialLogType, swarmingOutputType, syslogType}
	for _, lt := range allLogTypes {
		// For fxbug.dev/43355.
		ret = append(ret, []FailureModeCheck{
			&stringInLogCheck{String: "Timed out loading dynamic linker from fuchsia.ldsvc.Loader", Type: lt},
			&stringInLogCheck{String: "ERROR: AddressSanitizer", Type: lt, AttributeToTest: true},
			&stringInLogCheck{String: "ERROR: LeakSanitizer", Type: lt, AttributeToTest: true, ExceptBlocks: []*logBlock{
				// startString and endString should match string in //zircon/system/ulib/c/test/sanitizer/lsan-test.cc.
				{startString: "[===LSAN EXCEPT BLOCK START===]", endString: "[===LSAN EXCEPT BLOCK END===]"},
				// Kernel out-of-memory test "OOMHard" may report false positive leaks.
				{startString: "RUN   TestOOMHard", endString: "PASS: TestOOMHard"},
			}},
			&stringInLogCheck{String: "WARNING: ThreadSanitizer", Type: lt, AttributeToTest: true},
			&stringInLogCheck{String: "SUMMARY: UndefinedBehaviorSanitizer", Type: lt, AttributeToTest: true},
			// Match specific OOPS types before finally matching the generic type.
			&stringInLogCheck{String: "lockup_detector: no heartbeat from", Type: lt, AttributeToTest: true, ExceptBlocks: oopsExceptBlocks},
			&stringInLogCheck{String: "ZIRCON KERNEL OOPS", Type: lt, AttributeToTest: true, ExceptBlocks: oopsExceptBlocks},
			&stringInLogCheck{String: "ZIRCON KERNEL PANIC", AttributeToTest: true, Type: lt, ExceptBlocks: []*logBlock{
				// These tests intentionally trigger kernel panics.
				{startString: "RUN   TestBasicCrash", endString: "PASS: TestBasicCrash"},
				{startString: "RUN   TestReadUserMemoryViolation", endString: "PASS: TestReadUserMemoryViolation"},
				{startString: "RUN   TestExecuteUserMemoryViolation", endString: "PASS: TestExecuteUserMemoryViolation"},
				{startString: "RUN   TestPmmCheckerOopsAndPanic", endString: "PASS: TestPmmCheckerOopsAndPanic"},
				{startString: "RUN   TestCrashAssert", endString: "PASS: TestCrashAssert"},
				{startString: "RUN   TestKernelLockupDetectorFatalCriticalSection", endString: ": TestKernelLockupDetectorFatalCriticalSection"},
				{startString: "RUN   TestKernelLockupDetectorFatalHeartbeat", endString: ": TestKernelLockupDetectorFatalHeartbeat"},
				{startString: "RUN   TestMissingCmdlineEntropyPanics", endString: "PASS: TestMissingCmdlineEntropyPanics"},
				{startString: "RUN   TestIncompleteCmdlineEntropyPanics", endString: "PASS: TestIncompleteCmdlineEntropyPanics"},
				{startString: "RUN   TestDisabledJitterEntropyAndRequiredDoesntBoot", endString: "PASS: TestDisabledJitterEntropyAndRequiredDoesntBoot"},
				{startString: "RUN   TestDisabledJitterEntropyAndRequiredForReseedDoesntReachUserspace", endString: "PASS: TestDisabledJitterEntropyAndRequiredForReseedDoesntReachUserspace"},
			}},
			&stringInLogCheck{String: "double fault, halting", Type: lt},
			// This string can show up in some zbi tests.
			&stringInLogCheck{String: "entering panic shell loop", Type: lt, ExceptString: "ZBI-test-successful!"},
		}...)
	}

	ret = append(ret, []FailureModeCheck{
		// These may be in the output of tests, but the syslogType doesn't contain any test output.
		&stringInLogCheck{String: "ASSERT FAILED", Type: syslogType},
		&stringInLogCheck{String: "DEVICE SUSPEND TIMED OUT", Type: syslogType},
		// For fxbug.dev/61419.
		// Error is being logged at https://fuchsia.googlesource.com/fuchsia/+/675c6b9cc2452cd7108f075d91e048218b92ae69/garnet/bin/run_test_component/main.cc#431
		&stringInLogCheck{
			String: ".cmx canceled due to timeout.",
			Type:   swarmingOutputType,
			ExceptBlocks: []*logBlock{
				{
					startString: "[ RUN      ] RunFixture.TestTimeout",
					endString:   "RunFixture.TestTimeout (",
				},
			},
			OnlyOnStates: []string{"TIMED_OUT"},
		},
		&stringInLogCheck{
			String: "failed to resolve fuchsia-pkg://fuchsia.com/run_test_component#bin/run-test-component",
			Type:   swarmingOutputType,
		},
		&stringInLogCheck{
			String: "Got no package for fuchsia-pkg://",
			Type:   swarmingOutputType,
		},
	}...)
	return ret
}

// infraToolLogChecks returns all the checks for logs that are emitted by
// infrastructure host tools.
func infraToolLogChecks() []FailureModeCheck {
	return []FailureModeCheck{
		// For fxbug.dev/47649.
		&stringInLogCheck{String: "kvm run failed Bad address", Type: swarmingOutputType},
		// For fxbug.dev/44779.
		&stringInLogCheck{String: netutilconstants.CannotFindNodeErrMsg, Type: swarmingOutputType},
		// For fxbug.dev/51015.
		&stringInLogCheck{
			String:         bootserverconstants.FailedToSendErrMsg(bootserverconstants.CmdlineNetsvcName),
			Type:           swarmingOutputType,
			SkipPassedTask: true,
		},
		// For fxbug.dev/43188.
		&stringInLogCheck{String: "/dev/net/tun (qemu): Device or resource busy", Type: swarmingOutputType},
		// testrunner logs this when the serial socket goes away unexpectedly.
		&stringInLogCheck{String: ".sock: write: broken pipe", Type: swarmingOutputType},
		// For fxbug.dev/85596.
		&stringInLogCheck{String: "connect: no route to host", Type: swarmingOutputType},
		// For fxbug.dev/57463.
		&stringInLogCheck{
			String: fmt.Sprintf("%s: signal: segmentation fault", botanistconstants.QEMUInvocationErrorMsg),
			Type:   swarmingOutputType,
		},
		// For fxbug.dev/61452.
		&stringInLogCheck{
			String: fmt.Sprintf("botanist ERROR: %s", botanistconstants.FailedToResolveIPErrorMsg),
			Type:   swarmingOutputType,
		},
		// For fxbug.dev/65073.
		&stringInLogCheck{
			String: fmt.Sprintf("botanist ERROR: %s", botanistconstants.PackageRepoSetupErrorMsg),
			Type:   swarmingOutputType,
		},
		// For local package server failures.
		&stringInLogCheck{
			String: fmt.Sprintf("botanist ERROR: %s", botanistconstants.FailedToServeMsg),
			Type:   swarmingOutputType,
		},
		// For fxbug.dev/65073.
		&stringInLogCheck{
			String: fmt.Sprintf("botanist ERROR: %s", botanistconstants.SerialReadErrorMsg),
			Type:   swarmingOutputType,
		},
		// For fxbug.dev/68743.
		&stringInLogCheck{
			String: botanistconstants.FailedToCopyImageMsg,
			Type:   swarmingOutputType,
		},
		// For fxbug.dev/82454.
		&stringInLogCheck{
			String: botanistconstants.FailedToExtendFVMMsg,
			Type:   swarmingOutputType,
		},
		// Error is being logged at https://fuchsia.googlesource.com/fuchsia/+/559948a1a4cbd995d765e26c32923ed862589a61/src/storage/lib/paver/paver.cc#175
		&stringInLogCheck{
			String: "Failed to stream partitions to FVM",
			Type:   swarmingOutputType,
		},
		// Emitted by the GCS Go library during image download.
		&stringInLogCheck{
			String: bootserverconstants.BadCRCErrorMsg,
			Type:   swarmingOutputType,
			// This error is generally transient, so ignore it as long as the
			// download can be retried and eventually succeeds.
			SkipPassedTask: true,
		},
		// For fxbug.dev/89222.
		&stringInLogCheck{
			String: serialconstants.FailedToOpenSerialSocketMsg,
			Type:   swarmingOutputType,
		},
		// For fxbug.dev/89437
		&stringInLogCheck{
			String: serialconstants.FailedToFindCursorMsg,
			Type:   swarmingOutputType,
		},
		// For fxbug.dev/103197. Usually indicates an issue with the bot. If the bots
		// with the failures have been consistently failing with the same error, file
		// a go/fxif-bug for the suspected bad bots.
		&stringInLogCheck{
			String: "server canceled transfer: could not open file for writing",
			Type:   swarmingOutputType,
			// This error may appear as part of a test, so ignore unless it happens
			// during device setup which will cause a task failure.
			SkipPassedTask: true,
		},
		// This error is emitted by `fastboot` when it fails to write an image
		// to the disk. It is generally caused by ECC errors.
		&stringInLogCheck{
			String: "FAILED (remote: 'error writing the image')",
			Type:   swarmingOutputType,
		},
		// For https://fxbug.dev/96079.
		// This error usually means some kind of USB flakiness/instability when fastboot flashing.
		&stringInLogCheck{
			String: "FAILED (Status read failed (Protocol error))",
			Type:   swarmingOutputType,
		},
		// For fxbug.dev/53101.
		&stringInLogCheck{
			String: fmt.Sprintf("botanist ERROR: %s", botanistconstants.FailedToStartTargetMsg),
			Type:   swarmingOutputType,
		},
		// For fxbug.dev/51441.
		&stringInLogCheck{
			String: fmt.Sprintf("botanist ERROR: %s", botanistconstants.ReadConfigFileErrorMsg),
			Type:   swarmingOutputType,
		},
		// For fxbug.dev/59237.
		&stringInLogCheck{
			String: fmt.Sprintf("botanist ERROR: %s", sshutilconstants.TimedOutConnectingMsg),
			Type:   swarmingOutputType,
		},
		// For fxbug.dev/61420.
		&stringInLogCheck{
			String:       fmt.Sprintf("syslog: %s", syslogconstants.CtxReconnectError),
			Type:         swarmingOutputType,
			OnlyOnStates: []string{"TIMED_OUT"},
		},
		// For fxbug.dev/52719.
		// Kernel panics and other low-level errors often cause crashes that
		// manifest as SSH failures, so this check must come after all
		// Zircon-related errors to ensure tefmocheck attributes these crashes to
		// the actual root cause.
		&stringInLogCheck{
			String: fmt.Sprintf("testrunner ERROR: %s", testrunnerconstants.FailedToReconnectMsg),
			Type:   swarmingOutputType,
		},
		// For fxbug.dev/77689.
		&stringInLogCheck{
			String: testrunnerconstants.FailedToStartSerialTestMsg,
			Type:   swarmingOutputType,
		},
		// For fxbug.dev/92141.
		&stringInLogCheck{
			String: ffxutilconstants.TimeoutReachingTargetMsg,
			Type:   swarmingOutputType,
		},
		// For fxbug.dev/56651.
		// This error usually happens due to an SSH failure, so that error should take precedence.
		&stringInLogCheck{
			String: fmt.Sprintf("testrunner ERROR: %s", testrunnerconstants.FailedToRunSnapshotMsg),
			Type:   swarmingOutputType,
		},
		// General ffx error check.
		&stringInLogCheck{
			String: fmt.Sprintf("testrunner FATAL: %s", ffxutilconstants.CommandFailedMsg),
			Type:   swarmingOutputType,
		},
		&stringInLogCheck{
			String: fmt.Sprintf("testrunner ERROR: %s", ffxutilconstants.CommandFailedMsg),
			Type:   swarmingOutputType,
		},
		&stringInLogCheck{
			String: fmt.Sprintf("botanist ERROR: %s", ffxutilconstants.CommandFailedMsg),
			Type:   swarmingOutputType,
		},
		// This error happens when `botanist run` exceeds its timeout, e.g.
		// because many tests are taking too long.
		&stringInLogCheck{
			String: fmt.Sprintf("botanist ERROR: %s", botanistconstants.CommandExceededTimeoutMsg),
			Type:   swarmingOutputType,
		},
		// For fxbug.dev/94343.
		&stringInLogCheck{
			String: "There was an internal error running tests: Fidl(ClientChannelClosed { status: Status(PEER_CLOSED)",
			Type:   swarmingOutputType,
		},
		// This error happens when ffx test returns early and skips running some tests.
		&stringInLogCheck{
			String: fmt.Sprintf("testrunner FATAL: %s", testrunnerconstants.SkippedRunningTestsMsg),
			Type:   swarmingOutputType,
		},
	}
}
