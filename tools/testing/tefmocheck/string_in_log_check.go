package tefmocheck

import (
	"bytes"
	"fmt"
	"path"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/bootserver/bootserverconstants"
	botanistconstants "go.fuchsia.dev/fuchsia/tools/botanist/constants"
	"go.fuchsia.dev/fuchsia/tools/net/netutilconstants"
	testrunnerconstants "go.fuchsia.dev/fuchsia/tools/testing/testrunner/constants"
)

// stringInLogCheck checks if String is found in the log named LogName.
type stringInLogCheck struct {
	String string
	// ExceptString will cause Check() to return false if present.
	ExceptString string
	// ExceptBlock will cause Check() to return false if the string is only within this block.
	ExceptBlock *LogBlock
	Log         LogType
}

func (c stringInLogCheck) Check(to *TestingOutputs) bool {
	var toCheck []byte
	switch c.Log {
	case SerialLogType:
		toCheck = to.SerialLog
	case SwarmingOutputType:
		toCheck = to.SwarmingOutput
	case SyslogType:
		toCheck = to.Syslog
	}
	if c.ExceptString != "" && bytes.Contains(toCheck, []byte(c.ExceptString)) {
		return false
	}
	stringBytes := []byte(c.String)
	if c.ExceptBlock == nil {
		return bytes.Contains(toCheck, stringBytes)
	}
	blocks := bytes.Split(toCheck, []byte(c.ExceptBlock.StartString))
	// blocks[0] is everything before the first StartString.
	if bytes.Contains(blocks[0], stringBytes) {
		return true
	}
	for _, block := range blocks[1:] {
		// Each block will have started with the StartString of the ExceptBlock.
		subBlocks := bytes.SplitN(block, []byte(c.ExceptBlock.EndString), 2)
		if len(subBlocks) > 1 {
			// Check the logs after the EndString.
			if bytes.Contains(subBlocks[1], stringBytes) {
				return true
			}
		}
	}
	return false
}

func (c stringInLogCheck) Name() string {
	return path.Join("string_in_log", string(c.Log), strings.ReplaceAll(c.String, " ", "_"))
}

func driverHostCrash(hostName, exceptHost string) stringInLogCheck {
	c := stringInLogCheck{String: "<== fatal : process driver_host:" + hostName, Log: SerialLogType}
	if exceptHost != "" {
		c.ExceptString = "<== fatal : process driver_host:" + exceptHost
	}
	return c
}

// StringInLogsChecks returns checks to detect bad strings in certain logs.
func StringInLogsChecks() (ret []FailureModeCheck) {
	// For fxbug.dev/47649.
	ret = append(ret, stringInLogCheck{String: "kvm run failed Bad address", Log: SwarmingOutputType})
	// For fxbug.dev/44779.
	ret = append(ret, stringInLogCheck{String: netutilconstants.CannotFindNodeErrMsg, Log: SwarmingOutputType})
	// For fxbug.dev/51015.
	ret = append(ret, stringInLogCheck{String: bootserverconstants.FailedToSendErrMsg(bootserverconstants.CmdlineNetsvcName), Log: SwarmingOutputType})
	// For fxbug.dev/52719.
	ret = append(ret, stringInLogCheck{String: fmt.Sprintf("testrunner ERROR: %s", testrunnerconstants.FailedToReconnectMsg), Log: SwarmingOutputType})
	// For fxbug.dev/43188.
	ret = append(ret, stringInLogCheck{String: "/dev/net/tun (qemu): Device or resource busy", Log: SwarmingOutputType})
	// For fxbug.dev/53101.
	ret = append(ret, stringInLogCheck{String: fmt.Sprintf("botanist ERROR: %s", botanistconstants.FailedToStartTargetMsg), Log: SwarmingOutputType})
	// For fxbug.dev/51441.
	ret = append(ret, stringInLogCheck{String: fmt.Sprintf("botanist ERROR: %s", botanistconstants.ReadConfigFileErrorMsg), Log: SwarmingOutputType})
	// For fxbug.dev/43355
	ret = append(ret, stringInLogCheck{String: "Timed out loading dynamic linker from fuchsia.ldsvc.Loader", Log: SwarmingOutputType})
	// For fxbug.dev/53854
	ret = append(ret, driverHostCrash("composite-device", ""))
	ret = append(ret, driverHostCrash("pci", ""))
	// Don't fail if we see PDEV_DID_CRASH_TEST, defined in
	// zircon/system/ulib/ddk-platform-defs/include/ddk/platform-defs.h.
	// That's used for a test that intentionally crashes a driver host.
	ret = append(ret, driverHostCrash("pdev", "pdev:00:00:24"))
	// Catch-all for driver host crashes.
	ret = append(ret, driverHostCrash("", "pdev:00:00:24"))
	// These are rather generic. New checks should probably go above here so that they run before these.
	allLogTypes := []LogType{SerialLogType, SwarmingOutputType, SyslogType}
	for _, logType := range allLogTypes {
		ret = append(ret, stringInLogCheck{String: "ERROR: AddressSanitizer", Log: logType})
		ret = append(ret, stringInLogCheck{String: "ERROR: LeakSanitizer", Log: logType})
		ret = append(ret, stringInLogCheck{String: "SUMMARY: UndefinedBehaviorSanitizer", Log: logType})
		ret = append(ret, stringInLogCheck{
			String:      "ZIRCON KERNEL OOPS",
			Log:         logType,
			ExceptBlock: &LogBlock{StartString: " lock_dep_dynamic_analysis_tests ", EndString: " lock_dep_static_analysis_tests "},
		})
		ret = append(ret, stringInLogCheck{String: "ZIRCON KERNEL PANIC", Log: logType})
	}
	// These may be in the output of tests, but the syslog doesn't contain any test output.
	ret = append(ret, stringInLogCheck{String: "ASSERT FAILED", Log: SyslogType})
	ret = append(ret, stringInLogCheck{String: "DEVICE SUSPEND TIMED OUT", Log: SyslogType})
	return ret
}
