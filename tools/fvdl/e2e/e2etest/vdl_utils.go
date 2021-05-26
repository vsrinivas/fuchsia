// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package e2etest

import (
	"bufio"
	"fmt"
	"os"
	"os/exec"
	"regexp"
	"strings"
)

// GetProcessPID parses vdlOutput textproto and finds the recorded PID for the specified name.
func GetProcessPID(name, vdlOut string) string {
	file, err := os.Open(vdlOut)
	if err != nil {
		return ""
	}
	defer file.Close()
	processRegex := regexp.MustCompile(`\s+processes:\s+\{$`)
	processNameRegex := regexp.MustCompile(`\s+name:\s+"(?P<name>\w+)"$`)
	processPIDRegex := regexp.MustCompile(`\s+pid:\s+(?P<id>\d+)$`)

	foundProcess := false
	foundProcessName := false

	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		line := scanner.Text()
		if processRegex.MatchString(line) {
			foundProcess = true
			continue
		}
		if foundProcess && !foundProcessName {
			matched := processNameRegex.FindStringSubmatch(line)
			if len(matched) == 2 {
				if matched[1] == name {
					foundProcessName = true
				} else {
					foundProcessName = false
					foundProcess = false
				}
				continue
			}
		}
		if foundProcess && foundProcessName {
			matched := processPIDRegex.FindStringSubmatch(line)
			if len(matched) == 2 {
				return matched[1]
			}
			foundProcess = false
			foundProcessName = false
		}
	}
	return ""
}

// GetProcessPort parses vdlOutput textproto and finds the recorded port value for the specified name.
func GetProcessPort(name, vdlOut string) string {
	file, err := os.Open(vdlOut)
	if err != nil {
		return ""
	}
	defer file.Close()
	portRegex := regexp.MustCompile(`\s+ports:\s+\{$`)
	portNameRegex := regexp.MustCompile(`\s+name:\s+"(?P<name>\w+)"$`)
	portValueRegex := regexp.MustCompile(`\s+value:\s+(?P<id>\d+)$`)

	foundPort := false
	foundPortName := false

	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		line := scanner.Text()
		fmt.Println(line)
		if portRegex.MatchString(line) {
			foundPort = true
			continue
		}
		if foundPort && !foundPortName {
			matched := portNameRegex.FindStringSubmatch(line)
			if len(matched) == 2 {
				if matched[1] == name {
					foundPortName = true
				} else {
					foundPort = false
					foundPortName = false
				}
				continue
			}
		}
		if foundPort && foundPortName {
			matched := portValueRegex.FindStringSubmatch(line)
			if len(matched) == 2 {
				return matched[1]
			}
			foundPort = false
			foundPortName = false
		}
	}
	return ""
}

// IsEmuRunning checks if there is a running host qemu process that matches the emuPID.
func IsEmuRunning(emuPID string) bool {
	pid, err := exec.Command("pgrep", "qemu").Output()
	if err != nil {
		return false
	}
	pids := strings.Split(string(pid), "\n")
	for _, d := range pids {
		if d == emuPID {
			return true
		}
	}
	return false
}
