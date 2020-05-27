package tefmocheck

import (
	"bytes"
	"path"
	"strings"
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

// StringInLogsChecks returns checks to detect bad strings in certain logs.
func StringInLogsChecks() (ret []FailureModeCheck) {
	allLogTypes := []LogType{SerialLogType, SwarmingOutputType, SyslogType}
	for _, logType := range allLogTypes {
		ret = append(ret, stringInLogCheck{String: "ERROR: AddressSanitizer", Log: logType})
		ret = append(ret, stringInLogCheck{String: "ZIRCON KERNEL OOPS", Log: logType})
		ret = append(ret, stringInLogCheck{String: "ZIRCON KERNEL PANIC", Log: logType})
	}
	// These may be in the output of tests, but the syslog doesn't contain any test output.
	ret = append(ret, stringInLogCheck{String: "ASSERT FAILED", Log: SyslogType})
	ret = append(ret, stringInLogCheck{String: "DEVICE SUSPEND TIMED OUT", Log: SyslogType})
	// For fxbug.dev/47649.
	ret = append(ret, stringInLogCheck{String: "kvm run failed Bad address", Log: SwarmingOutputType})
	return ret
}
