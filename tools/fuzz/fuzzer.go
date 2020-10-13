// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzz

import (
	"bufio"
	"fmt"
	"io"
	"path"
	"regexp"
	"strconv"
	"strings"

	"github.com/golang/glog"
)

// A Fuzzer represents a fuzzer present on an instance.
type Fuzzer struct {
	build Build
	// Name is `package/binary`
	Name string

	pkg     string
	cmx     string
	url     string
	args    []string
	options map[string]string
}

var expectedFuzzerReturnCodes = [...]int{0, 1, 77}

// NewFuzzer constructs a fuzzer object with the given pkg/fuzzer name
func NewFuzzer(build Build, pkg, fuzzer string) *Fuzzer {
	return &Fuzzer{
		build: build,
		Name:  fmt.Sprintf("%s/%s", pkg, fuzzer),
		pkg:   pkg,
		cmx:   fmt.Sprintf("%s.cmx", fuzzer),
		url:   fmt.Sprintf("fuchsia-pkg://fuchsia.com/%s#meta/%s.cmx", pkg, fuzzer),
	}
}

// AbsPath returns the absolute target path for a given relative path in a
// fuzzer package. The path may differ depending on whether it is identified as
// a resource, data, or neither.
func (f *Fuzzer) AbsPath(relpath string) string {
	if strings.HasPrefix(relpath, "/") {
		return relpath
	} else if strings.HasPrefix(relpath, "pkg/") {
		return fmt.Sprintf("/pkgfs/packages/%s/0/%s", f.pkg, relpath[4:])
	} else if strings.HasPrefix(relpath, "data/") {
		return fmt.Sprintf("/data/r/sys/fuchsia.com:%s:0#meta:%s/%s", f.pkg, f.cmx, relpath[5:])
	} else {
		return fmt.Sprintf("/%s", relpath)
	}
}

// Parse command line arguments for the fuzzer. For '-key=val' style options,
// the last 'val' for a given 'key' is used.
func (f *Fuzzer) Parse(args []string) {
	f.options = make(map[string]string)
	re := regexp.MustCompile(`^-([^-=\s]*)=([^-=\s]*)$`)
	for _, arg := range args {
		submatch := re.FindStringSubmatch(arg)
		if submatch == nil {
			f.args = append(f.args, arg)
		} else {
			f.options[submatch[1]] = submatch[2]
		}
	}
}

func scanForPIDs(conn Connector, out io.WriteCloser, in io.Reader) chan error {
	scanErr := make(chan error, 1)

	go func() {
		defer out.Close() // Propagate the EOF, so the symbolizer terminates properly

		// mutRegex detects output from
		// MutationDispatcher::PrintMutationSequence
		// (compiler-rt/lib/fuzzer/FuzzerMutate.cpp), which itself is called
		// from Fuzzer::DumpCurrentUnit (compiler-rt/lib/fuzzer/FuzzerLoop.cpp)
		// as part of exit/crash callbacks
		mutRegex := regexp.MustCompile(`^MS: [0-9]*`)
		pidRegex := regexp.MustCompile(`^==([0-9]+)==`)
		sawMut := false
		sawPid := false
		scanner := bufio.NewScanner(in)
		pid := 0
		for scanner.Scan() {
			line := scanner.Text()
			if m := pidRegex.FindStringSubmatch(line); m != nil {
				if sawPid {
					glog.Warningf("Saw multiple PIDs")
				}
				sawPid = true

				pid, _ = strconv.Atoi(m[1]) // guaranteed parseable due to regex
				glog.Infof("Found fuzzer PID: %d", pid)
			}
			if mutRegex.MatchString(line) {
				if sawMut {
					glog.Warningf("Saw multiple mutation sequences")
				}
				sawMut = true

				glog.Infof("Found mutation sequence: %s", line)
				if pid == 0 {
					glog.Warningf("WARNING: failed to fetch syslog: missing pid")
					// Include this warning inline so it is visible in fuzzer logs
					fmt.Fprintf(out, "WARNING: failed to fetch syslog: missing pid\n")
				} else {
					log, err := conn.GetSysLog(pid)
					if err != nil {
						glog.Warningf("WARNING: failed to fetch syslog: %s", err)
						// Include this warning inline so it is visible in fuzzer logs
						fmt.Fprintf(out, "WARNING: failed to fetch syslog: %s\n", err)
					} else {
						io.WriteString(out, log+"\n")
					}
				}
			}
			io.WriteString(out, line+"\n")
		}

		scanErr <- scanner.Err()
	}()

	return scanErr
}

func scanForArtifacts(out io.WriteCloser, in io.Reader,
	artifactPrefix, hostArtifactDir string) (chan error, chan []string) {

	// Only replace the directory part of the artifactPrefix, which is
	// guaranteed to be at least "data"
	artifactDir := path.Dir(artifactPrefix)

	scanErr := make(chan error, 1)
	artifactsCh := make(chan []string, 1)

	go func() {
		defer out.Close() // Propagate the EOF, so the symbolizer terminates properly

		artifacts := []string{}

		artifactRegex := regexp.MustCompile(`Test unit written to (\S+)`)
		scanner := bufio.NewScanner(in)
		for scanner.Scan() {
			line := scanner.Text()
			if m := artifactRegex.FindStringSubmatch(line); m != nil {
				glog.Infof("Found artifact: %s", m[1])
				artifacts = append(artifacts, m[1])
				if hostArtifactDir != "" {
					line = strings.Replace(line, artifactDir, hostArtifactDir, 2)
				}
			}
			io.WriteString(out, line+"\n")
		}

		scanErr <- scanner.Err()
		artifactsCh <- artifacts
	}()

	return scanErr, artifactsCh
}

// Run the fuzzer, sending symbolized output to `out` and returning a list of
// any referenced artifacts (e.g. crashes) as absolute paths. If provided,
// `hostArtifactDir` will be used to transparently rewrite artifact_prefix
// references in artifact paths in the output log.
func (f *Fuzzer) Run(conn Connector, out io.Writer, hostArtifactDir string) ([]string, error) {
	if f.options == nil {
		return nil, fmt.Errorf("Run called on Fuzzer before Parse")
	}

	// Ensure artifact_prefix will be writable, and fall back to default if not
	// specified
	if artPrefix, ok := f.options["artifact_prefix"]; ok {
		if !strings.HasPrefix(strings.TrimLeft(artPrefix, "/"), "data/") {
			return nil, fmt.Errorf("artifact_prefix not in data/ namespace: %q", artPrefix)
		}
	} else {
		f.options["artifact_prefix"] = "data/"
	}

	cmdline := []string{f.url}
	for k, v := range f.options {
		cmdline = append(cmdline, fmt.Sprintf("-%s=%s", k, v))
	}
	for _, arg := range f.args {
		cmdline = append(cmdline, arg)
	}
	cmd := conn.Command("run", cmdline...)
	fuzzerOutput, err := cmd.StdoutPipe()
	if err != nil {
		return nil, err
	}
	if err := cmd.Start(); err != nil {
		return nil, err
	}

	fromPIDScanner, toArtifactScanner := io.Pipe()

	// Start goroutine to scan for PID and insert syslog
	pidScanErr := scanForPIDs(conn, toArtifactScanner, fuzzerOutput)

	fromArtifactScanner, toSymbolizer := io.Pipe()

	// Start goroutine to check/rewrite artifact paths
	artifactScanErr, artifactCh := scanForArtifacts(toSymbolizer,
		fromPIDScanner, f.options["artifact_prefix"], hostArtifactDir)

	// Start symbolizer goroutine, connected to the output
	symErr := make(chan error, 1)
	go func(in io.Reader) {
		symErr <- f.build.Symbolize(in, out)
	}(fromArtifactScanner)

	err = cmd.Wait()
	glog.Infof("Fuzzer run has completed")

	// Check the returncode (though this might be modified
	// by libfuzzer args and thus be unreliable)
	if cmderr, ok := err.(*InstanceCmdError); ok {
		found := false
		for _, code := range expectedFuzzerReturnCodes {
			if cmderr.ReturnCode == code {
				found = true
				break
			}
		}
		if !found {
			return nil, fmt.Errorf("unexpected return code: %s", err)
		}
	} else if err != nil {
		return nil, fmt.Errorf("failed during wait: %s", err)
	}

	// Check for any errors in the goroutines
	if err := <-symErr; err != nil {
		return nil, fmt.Errorf("failed during symbolization: %s", err)
	}
	if err := <-pidScanErr; err != nil {
		return nil, fmt.Errorf("failed during PID scanning: %s", err)
	}
	if err := <-artifactScanErr; err != nil {
		return nil, fmt.Errorf("failed during artifact scanning: %s", err)
	}

	var artifacts []string
	for _, artifact := range <-artifactCh {
		artifacts = append(artifacts, f.AbsPath(artifact))
	}

	return artifacts, nil
}
