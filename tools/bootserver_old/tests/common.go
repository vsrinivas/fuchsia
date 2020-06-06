package bootserver

import (
	"bufio"
	"context"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"
	"testing"
	"time"

	"go.fuchsia.dev/fuchsia/src/testing/qemu"
)

// The default nodename given to an target with the default QEMU MAC address.
const DefaultNodename = "swarm-donut-petri-acre"

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

func FirmwarePath(t *testing.T) string {
	// QEMU doesn't know how to write firmware so the contents don't matter,
	// it just has to be a real file. It does get sent over the network though
	// so use a small file to avoid long transfers.
	return ToolPath(t, "fake_firmware")
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

// Starts a QEMU instance with the given kernel commandline args.
// Returns the qemu.Instance and a cleanup function to call when finished.
func StartQemu(t *testing.T, appendCmdline string, modeString string) (*qemu.Instance, func()) {
	distro, err := qemu.Unpack()
	if err != nil {
		t.Fatalf("Failed to unpack QEMU: %s", err)
	}
	arch, err := distro.TargetCPU()
	if err != nil {
		t.Fatalf("Failed to get distro CPU: %s", err)
	}
	zbi, err := zbiPath()
	if err != nil {
		t.Fatalf("Failed to get ZBI path: %s", err)
	}

	instance := distro.Create(qemu.Params{
		Arch:          arch,
		ZBI:           zbi,
		AppendCmdline: appendCmdline,
		Networking:    true,
	})

	instance.Start()
	if err != nil {
		t.Fatalf("Failed to start QEMU instance: %s", err)
	}

	// Make sure netsvc in expected mode.
	instance.WaitForLogMessage("netsvc: running in " + modeString + " mode")

	// Make sure netsvc is booted.
	instance.WaitForLogMessage("netsvc: start")

	return instance, func() {
		instance.Kill()
		distro.Delete()
	}
}
