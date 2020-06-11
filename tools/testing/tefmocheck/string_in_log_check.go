package tefmocheck

import (
	"bytes"
	"fmt"
	"path"
	"strings"
	"syscall"

	"go.fuchsia.dev/fuchsia/tools/bootserver/bootserverconstants"
	botanistconstants "go.fuchsia.dev/fuchsia/tools/botanist/constants"
	"go.fuchsia.dev/fuchsia/tools/net/netutilconstants"
	testrunnerconstants "go.fuchsia.dev/fuchsia/tools/testing/testrunner/constants"
)

// stringInLogCheck checks if String is found in the log named LogName.
type stringInLogCheck struct {
	String string
	Log    LogType
}

func (c stringInLogCheck) Check(to *TestingOutputs) bool {
	switch c.Log {
	case SerialLogType:
		return bytes.Contains(to.SerialLog, []byte(c.String))
	case SwarmingOutputType:
		return bytes.Contains(to.SwarmingOutput, []byte(c.String))
	case SyslogType:
		return bytes.Contains(to.Syslog, []byte(c.String))
	}
	return false
}

func (c stringInLogCheck) Name() string {
	return path.Join("string_in_log", string(c.Log), strings.ReplaceAll(c.String, " ", "_"))
}

func driverHostCrash(hostName string) stringInLogCheck {
	return stringInLogCheck{String: "<== fatal : process driver_host:" + hostName, Log: SerialLogType}
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
	ret = append(ret, stringInLogCheck{String: fmt.Sprintf("/dev/net/tun (qemu): %s", syscall.EBUSY.Error()), Log: SwarmingOutputType})
	// For fxbug.dev/53101.
	ret = append(ret, stringInLogCheck{String: fmt.Sprintf("botanist ERROR: %s", botanistconstants.FailedToStartTargetMsg), Log: SwarmingOutputType})
	// For fxbug.dev/51441.
	ret = append(ret, stringInLogCheck{String: fmt.Sprintf("botanist ERROR: %s", botanistconstants.ReadConfigFileErrorMsg), Log: SwarmingOutputType})
	// For fxbug.dev/43355
	ret = append(ret, stringInLogCheck{String: "Timed out loading dynamic linker from fuchsia.ldsvc.Loader", Log: SwarmingOutputType})
	// For fxbug.dev/53854
	ret = append(ret, driverHostCrash("composite-device"))
	ret = append(ret, driverHostCrash("pci"))
	ret = append(ret, driverHostCrash("pdev"))
	// Catch-all driver host crashes
	ret = append(ret, driverHostCrash(""))
	// These are rather generic. New checks should probably go above here so that they run before these.
	allLogTypes := []LogType{SerialLogType, SwarmingOutputType, SyslogType}
	for _, logType := range allLogTypes {
		ret = append(ret, stringInLogCheck{String: "ERROR: AddressSanitizer", Log: logType})
		ret = append(ret, stringInLogCheck{String: "ZIRCON KERNEL OOPS", Log: logType})
		ret = append(ret, stringInLogCheck{String: "ZIRCON KERNEL PANIC", Log: logType})
	}
	// These may be in the output of tests, but the syslog doesn't contain any test output.
	ret = append(ret, stringInLogCheck{String: "ASSERT FAILED", Log: SyslogType})
	ret = append(ret, stringInLogCheck{String: "DEVICE SUSPEND TIMED OUT", Log: SyslogType})
	return ret
}
