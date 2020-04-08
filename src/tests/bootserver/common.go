package bootserver

import (
	"bufio"
	"context"
	"io"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"
	"testing"
	"time"

	"fuchsia.googlesource.com/testing/qemu"
)

// The default nodename given to an target with the default QEMU MAC address.
const DefaultNodename = "swarm-donut-petri-acre"

var distro qemu.Distribution
var Instance *qemu.Instance

type LogMatch struct {
	Pattern     string
	ShouldMatch bool
}

func zbiPath() (string, error) {
	ex, err := os.Executable()
	if err != nil {
		return "", err
	}
	exPath := filepath.Dir(ex)
	return filepath.Join(exPath, "../fuchsia.zbi"), nil
}

func ToolPath(t *testing.T, name string) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
		return ""
	}
	exPath := filepath.Dir(ex)
	return filepath.Join(exPath, "test_data", "bootserver_tools", name)
}

func matchPattern(t *testing.T, pattern string, reader *bufio.Reader) bool {
	for {
		line, err := reader.ReadString('\n')
		if err != nil && err == io.EOF {
			break
		}
		t.Logf("matchPattern: %s", line)
		if strings.Contains(line, pattern) {
			return true
		}
	}
	return false
}

func CmdWithOutput(t *testing.T, name string, arg ...string) []byte {
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	cmd := exec.CommandContext(ctx, name, arg...)
	out, err := cmd.Output()
	if err != nil {
		t.Errorf("%s failed %s, err=%s", name, out, err)
		return nil
	}
	return out
}

func CmdSearchLog(t *testing.T, logPatterns []LogMatch,
	name string, arg ...string) {

	found := false

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	cmd := exec.CommandContext(ctx, name, arg...)

	cmderr, err := cmd.StderrPipe()
	if err != nil {
		t.Errorf("Failed to stdout %s", err)
	}
	readerErr := bufio.NewReader(cmderr)

	if err := cmd.Start(); err != nil {
		t.Errorf("Failed to start %s", err)
	}

	for _, logPattern := range logPatterns {
		match := matchPattern(t, logPattern.Pattern, readerErr)
		if match != logPattern.ShouldMatch {
			found = false
			t.Errorf("Log pattern \"%s\" mismatch. Expected - %t, actual - %t",
				logPattern.Pattern, logPattern.ShouldMatch, match)
			break
		}
		found = true
	}

	if err := cmd.Wait(); err != nil {
		t.Logf("Failed to wait on task %s", err)
	}

	if ctx.Err() == context.DeadlineExceeded {
		t.Errorf("%s timed out err=%s", name, ctx.Err())
	} else if !found {
		t.Errorf("%s failed to match logs", name)
	} else {
		t.Logf("%s worked as expected", name)
	}
}

func AttemptPaveNoBind(t *testing.T, shouldWork bool) {
	// Get the node ipv6 address
	out := CmdWithOutput(t, ToolPath(t, "netls"))
	// Extract the ipv6 from the netls output
	regexString := DefaultNodename + ` \((?P<ipv6>.*)\)`
	match := regexp.MustCompile(regexString).FindStringSubmatch(string(out))
	if len(match) != 2 {
		t.Errorf("Node %s not found in netls output - %s", DefaultNodename, out)
		return
	}

	var logPattern []LogMatch
	if shouldWork {
		paveWorksPattern := []LogMatch{
			{"Sending request to ", true},
			{"Received request from ", true},
			{"Proceeding with nodename ", true},
			{"Transfer starts", true},
		}
		logPattern = paveWorksPattern
	} else {
		paveFailsPattern := []LogMatch{
			{"Sending request to ", true},
			{"Received request from ", false},
			{"Proceeding with nodename ", false},
			{"Transfer starts", false},
		}
		logPattern = paveFailsPattern
	}

	CmdSearchLog(
		t, logPattern,
		ToolPath(t, "bootserver"), "--fvm", "\"dummy.blk\"",
		"--no-bind", "-a", match[1], "-1", "--fail-fast")

}

func startQemuInstance(appendCmdline string, modeString string) error {
	distro, err := qemu.Unpack()
	if err != nil {
		return err
	}
	arch, err := distro.TargetCPU()
	if err != nil {
		return err
	}
	zbi, err := zbiPath()
	if err != nil {
		return err
	}

	Instance = distro.Create(qemu.Params{
		Arch:          arch,
		ZBI:           zbi,
		AppendCmdline: appendCmdline,
		Networking:    true,
	})

	Instance.Start()
	if err != nil {
		return err
	}

	// Make sure netsvc in expected mode.
	Instance.WaitForLogMessage("netsvc: running in " + modeString + " mode")

	// Make sure netsvc is booted.
	Instance.WaitForLogMessage("netsvc: start")

	return nil
}

func stopQemuInstance() {
	Instance.Kill()
	distro.Delete()
}

// Tests should provide a custom main which calls RunTests() e.g.:
//
// func TestMain(m *testing.M) {
// 	 bootserver.RunTests(m, "netsvc.all-features=true, netsvc.netboot=true")
// }
//
// Note that RunTests() calls os.Exit() so will never return.
func RunTests(m *testing.M, kernelArgs string) {
	err := startQemuInstance(kernelArgs, "full")
	if err != nil {
		log.Fatalf("Failed to start QEMU: %v", err)
	}

	result := m.Run()

	stopQemuInstance()
	os.Exit(result)
}
