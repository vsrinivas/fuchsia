package main

import (
	"bufio"
	"context"
	"io"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"testing"
	"time"

	"fuchsia.googlesource.com/testing/qemu"
)

// The default nodename given to an target with the default QEMU MAC address.
const defaultNodename = "swarm-donut-petri-acre"

type logMatch struct {
	pattern     string
	shouldMatch bool
}

func zbiPath(t *testing.T) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
		return ""
	}
	exPath := filepath.Dir(ex)
	return filepath.Join(exPath, "../fuchsia.zbi")
}

func toolPath(t *testing.T, name string) string {
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
		if strings.Contains(line, pattern) {
			return true
		}
	}
	return false
}

func cmdWithOutput(t *testing.T, name string, arg ...string) []byte {
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

func cmdSearchLog(t *testing.T, logPatterns []logMatch,
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
		match := matchPattern(t, logPattern.pattern, readerErr)
		if match != logPattern.shouldMatch {
			found = false
			t.Errorf("Log pattern \"%s\" mismatch. Expected - %t, actual - %t",
				logPattern.pattern, logPattern.shouldMatch, match)
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

func setupQemu(t *testing.T, appendCmdline string, modeString string) (*qemu.Instance, func()) {
	distro, err := qemu.Unpack()
	if err != nil {
		t.Fatal(err)
	}
	arch, err := distro.TargetCPU()
	if err != nil {
		t.Fatal(err)
	}

	i := distro.Create(qemu.Params{
		Arch:          arch,
		ZBI:           zbiPath(t),
		AppendCmdline: appendCmdline,
		Networking:    true,
	})

	i.Start()
	if err != nil {
		t.Fatal(err)
	}

	// Make sure netsvc in expected mode.
	i.WaitForLogMessage("netsvc: running in " + modeString + " mode")

	// Make sure netsvc is booted.
	i.WaitForLogMessage("netsvc: start")

	return i, func() {
		i.Kill()
		distro.Delete()
	}
}

func attemptPaveNoBind(t *testing.T, shouldWork bool) {
	// Get the node ipv6 address
	out := cmdWithOutput(t, toolPath(t, "netls"))
	// Extract the ipv6 from the netls output
	regexString := defaultNodename + ` \((?P<ipv6>.*)\)`
	match := regexp.MustCompile(regexString).FindStringSubmatch(string(out))
	if len(match) != 2 {
		t.Errorf("Node %s not found in netls output - %s", defaultNodename, out)
		return
	}

	var logPattern []logMatch
	if shouldWork {
		paveWorksPattern := []logMatch{
			{"Sending request to ", true},
			{"Received request from ", true},
			{"Proceeding with nodename ", true},
			{"Transfer starts", true},
		}
		logPattern = paveWorksPattern
	} else {
		paveFailsPattern := []logMatch{
			{"Sending request to ", true},
			{"Received request from ", false},
			{"Proceeding with nodename ", false},
			{"Transfer starts", false},
		}
		logPattern = paveFailsPattern
	}

	cmdSearchLog(
		t, logPattern,
		toolPath(t, "bootserver"), "--fvm", "\"dummy.blk\"",
		"--no-bind", "-a", match[1], "-1", "--fail-fast")

}

func TestAdvertFrequency(t *testing.T) {
	_, cleanup := setupQemu(t, "netsvc.all-features=true, netsvc.netboot=true", "full")
	defer cleanup()

	// Get the node ipv6 address
	out := cmdWithOutput(t, toolPath(t, "netls"))
	// Extract the ipv6 from the netls output
	regexString := defaultNodename + ` \((.*)/(.*)\)`
	match := regexp.MustCompile(regexString).FindSubmatch(out)
	if len(match) != 3 {
		t.Fatalf("node %s not found in netls output - %s", defaultNodename, out)
	}
	index, err := strconv.Atoi(string(match[2]))
	if err != nil {
		t.Fatal(err)
	}
	ifi, err := net.InterfaceByIndex(index)
	if err != nil {
		t.Fatal(err)
	}

	conn, err := net.ListenMulticastUDP("udp6", ifi, &net.UDPAddr{
		IP:   net.IPv6linklocalallnodes,
		Port: 33331,
	})
	if err != nil {
		t.Fatal(err)
	}
	ip := net.ParseIP(string(match[1]))
	if ip == nil {
		t.Fatalf("can't parse %s as IP", match[1])
	}
	for i := 0; i < 50; i++ {
		if _, err := conn.WriteToUDP(nil, &net.UDPAddr{
			IP:   ip,
			Port: 1337,
			Zone: strconv.Itoa(index),
		}); err != nil {
			t.Fatal(err)
		}
	}
	if err := conn.SetDeadline(time.Now().Add(800 * time.Millisecond)); err != nil {
		t.Fatal(err)
	}
	var dst [100]byte
	for i := 0; i < 10; i++ {
		n, addr, err := conn.ReadFromUDP(dst[:])
		if i == 0 {
			if err != nil {
				t.Fatal(err)
			}
			continue
		}
		if err == nil {
			t.Errorf("expected one advertisement packet, got %d bytes from %s on iteration %d", n, addr, i)
			continue
		}
		if tErr, ok := err.(net.Error); !ok || !tErr.Timeout() {
			t.Errorf("expected timeout error on iteration %d, got %s", i, err)
			continue
		}
		break
	}
}

func TestPaveNoBind(t *testing.T) {
	_, cleanup := setupQemu(t, "netsvc.all-features=true, netsvc.netboot=true", "full")
	defer cleanup()

	// Test that advertise request is serviced and paving starts as netsvc.netboot=true
	attemptPaveNoBind(t, true)
}

func TestPaveNoBindFailure(t *testing.T) {
	_, cleanup := setupQemu(t, "netsvc.all-features=true, netsvc.netboot=false", "full")
	defer cleanup()

	// Test that advertise request is NOT serviced and paving does NOT start
	// as netsvc.netboot=false
	attemptPaveNoBind(t, false)
}

func TestInitPartitionTables(t *testing.T) {
	_, cleanup := setupQemu(t, "netsvc.all-features=true, netsvc.netboot=true", "full")
	defer cleanup()

	logPattern := []logMatch{
		{"Received request from ", true},
		{"Proceeding with nodename ", true},
		{"Transfer starts", true},
		{"Transfer ends successfully", true},
		{"Issued reboot command to", false},
	}

	cmdSearchLog(
		t, logPattern,
		toolPath(t, "bootserver"), "-n", defaultNodename,
		"--init-partition-tables", "/dev/class/block/000", "-1", "--fail-fast")
}
