// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzz

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

const mockFuzzerPid = 414141

// Used for keeping history of Put/Get calls
type transferCmd struct {
	src, dst string
}

// mockConnector is used in Fuzzer and Instance tests, and returned by mockLauncher.Start()
type mockConnector struct {
	connected                bool
	shouldFailToConnectCount uint
	shouldFailToExecuteCount uint
	shouldFailToGetSysLog    bool

	ffxIsolateDir string

	// Store history of Get/Puts to enable basic checks
	PathsGot           []transferCmd
	PathsPut           []transferCmd
	LastPutFileContent string

	// List of paths for which IsDir() should return true
	Dirs []string

	// Store history of commands run on this connection
	CmdHistory     []string
	FuzzCtlHistory []string
	FfxHistory     []string

	CleanedUp bool
}

func NewMockConnector(t *testing.T) *mockConnector {
	return &mockConnector{ffxIsolateDir: t.TempDir()}
}

func (c *mockConnector) Connect() error {
	if c.connected {
		return fmt.Errorf("Connect called when already connected")
	}

	if c.shouldFailToConnectCount > 0 {
		c.shouldFailToConnectCount -= 1
		return fmt.Errorf("Intentionally broken Connector")
	}

	c.connected = true
	return nil
}

func (c *mockConnector) Close() {
	c.connected = false
}

func (c *mockConnector) RmDir(path string) error {
	return c.Command("rm", "-rf", path).Run()
}

func (c *mockConnector) IsDir(path string) (bool, error) {
	if strings.HasSuffix(path, invalidPath) {
		return false, os.ErrNotExist
	}

	for _, dir := range c.Dirs {
		if path == dir {
			return true, nil
		}
	}
	return false, nil
}

func (c *mockConnector) Cleanup() {
	c.CleanedUp = true
}

func (c *mockConnector) Command(name string, args ...string) InstanceCmd {
	shouldFail := c.shouldFailToExecuteCount > 0
	if shouldFail {
		c.shouldFailToExecuteCount -= 1
	}
	return &mockInstanceCmd{connector: c, name: name, args: args, shouldFail: shouldFail}
}

func (c *mockConnector) Get(targetSrc string, hostDst string) error {
	fileInfo, err := os.Stat(hostDst)
	if err != nil {
		return fmt.Errorf("error stat-ing dest %q: %s", hostDst, err)
	}

	if !fileInfo.IsDir() {
		return fmt.Errorf("host dest is not a dir")
	}

	c.PathsGot = append(c.PathsGot, transferCmd{targetSrc, hostDst})

	return nil
}

func (c *mockConnector) Put(hostSrc string, targetDst string) error {
	srcList, err := filepath.Glob(hostSrc)
	if err != nil {
		return fmt.Errorf("error globbing source %q: %s", hostSrc, err)
	}

	if len(srcList) == 0 {
		return fmt.Errorf("no matches for glob %q", hostSrc)
	}

	for _, src := range srcList {
		fileInfo, err := os.Stat(src)
		if err != nil {
			return fmt.Errorf("error stat-ing source %q: %s", src, err)
		}

		// If a file was Put, save its contents
		if !fileInfo.IsDir() {
			data, err := os.ReadFile(src)
			if err != nil {
				return fmt.Errorf("error reading file: %s", err)
			}

			c.LastPutFileContent = string(data)
		}

		c.PathsPut = append(c.PathsPut, transferCmd{src, targetDst})
	}

	return nil
}

func (c *mockConnector) GetSysLog(pid int) (string, error) {
	if c.shouldFailToGetSysLog {
		return "syslog failure", fmt.Errorf("Intentionally broken Connector")
	}

	// Only return logs for the PID we expect
	if pid != mockFuzzerPid {
		return "", nil
	}

	// TODO(fxbug.dev/45425): more realistic test data
	lines := []string{
		fmt.Sprintf("syslog for %d", pid),
		"[1234.5][klog] INFO: {{{0x41}}}",
	}
	return strings.Join(lines, "\n"), nil
}

func (c *mockConnector) FfxRun(outputDir string, args ...string) (string, error) {
	c.FfxHistory = append(c.FfxHistory, strings.Join(args, " "))
	return strings.Join(args, " "), nil
}

// Whether, based on PathsPut, the target path should exist (assumes only files are put)
func (c *mockConnector) TargetPathExists(path string) bool {
	for _, put := range c.PathsPut {
		targetPath := filepath.Join(put.dst, filepath.Base(put.src))
		if targetPath == path {
			return true
		}
	}
	return false
}

func (c *mockConnector) FfxCommand(outputDir string, args ...string) (*exec.Cmd, error) {
	// Used only by Fuzzer.Run, which needs a full subprocess to work with
	args = append([]string{"--target", "some-target",
		"--isolate-dir", c.ffxIsolateDir}, args...)
	if outputDir != "" {
		args = append(args, "-o", outputDir)
	}

	// Instead of fully rewriting cache paths, which is impossible since we
	// don't implement an actual cache here, just set any unknown ones to an
	// invalidPath.
	for j, arg := range args {
		if !strings.HasPrefix(arg, cachePrefix) {
			continue
		}
		if !c.TargetPathExists(arg) {
			args[j] = invalidPath
		}
	}

	cmd := mockCommand("ffx", args...)
	return cmd, nil
}
