// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzz

import (
	"errors"
	"fmt"
	"io"
	"regexp"
	"strings"
	"time"
)

// mockInstanceCmd is the type returned by mockConnector.Command()
type mockInstanceCmd struct {
	connector  *mockConnector
	name       string
	args       []string
	running    bool
	pipeOut    *io.PipeWriter
	pipeErr    *io.PipeWriter
	errCh      chan error
	timeout    time.Duration
	shouldFail bool
}

func (c *mockInstanceCmd) getOutput() ([]byte, error) {
	var args []string
	switch c.name {
	case "run":
		args = c.args
	case "fuzz_ctl":
		if c.args[0] != "run_libfuzzer" {
			return nil, fmt.Errorf("unexpected run_libfuzzer subcommand: %q", c.args[0])
		}
		args = c.args[1:]
	default:
		return nil, fmt.Errorf("unknown command: %q", c.name)
	}

	re := regexp.MustCompile(`fuchsia-pkg://fuchsia\.com/([^#]+)#meta/([^\.]+)\.cmx?`)
	m := re.FindStringSubmatch(args[0])
	if m == nil {
		return nil, fmt.Errorf("unexpected %s argument: %q", c.name, args[0])
	}
	fuzzerName := fmt.Sprintf("%s/%s", m[1], m[2])

	// Look up arguments that we want to test
	var artifactPrefix string
	var mergeFile string
	var corpusPath string
	for _, arg := range args[1:] {
		// Save first non-option arg
		if corpusPath == "" && !strings.HasPrefix(arg, "-") {
			corpusPath = arg
			continue
		}

		if parts := strings.Split(arg, "="); parts[0] == "-artifact_prefix" {
			artifactPrefix = parts[1]
		} else if parts[0] == "-merge_control_file" {
			mergeFile = parts[1]
		}
	}
	if artifactPrefix == "" {
		return nil, fmt.Errorf("%s command missing artifact_prefix option: %q", c.name, args)
	}
	artifactLine := fmt.Sprintf("artifact_prefix='%s'; "+
		"Test unit written to %scrash-1312", artifactPrefix, artifactPrefix)

	if corpusPath == "" {
		return nil, fmt.Errorf("%s command missing output corpus dir: %q", c.name, args)
	}
	corpusLine := fmt.Sprintf("INFO:        4 files found in %s", corpusPath)

	var output []string
	switch fuzzerName {
	case "cff/fuzzer":
		break
	case "foo/bar":
		break
	case "fail/nopid":
		// No PID
		output = []string{
			fmt.Sprintf("running %v", args),
			"MS: ", // mut
			"Deadly signal",
			artifactLine,
		}
		return []byte(strings.Join(output, "\n") + "\n"), nil
	case "fail/notfound":
		return nil, &InstanceCmdError{ReturnCode: 127, Command: c.name, Stderr: "not found"}
	default:
		output = []string{fmt.Sprintf("unknown fuzzer %q", fuzzerName)}
		return []byte(strings.Join(output, "\n") + "\n"), nil
	}

	// Create 64k of filler to ensure we saturate any pipes on the
	// output that aren't being properly serviced.
	filler := make([]string, 1024)
	for j := 0; j < 1024; j++ {
		filler[j] = strings.Repeat("data", 64/4)
	}
	output = append(filler,
		fmt.Sprintf("running %v", args),
		corpusLine,
		"Running: "+corpusPath+"/testcase",
		fmt.Sprintf("==%d==", mockFuzzerPid),
		"MS: ", // mut
		"Deadly signal",
		artifactLine,
	)

	// This wouldn't actually be emitted during a non-merge run, but we
	// want to exercise an option with a path
	if mergeFile != "" {
		output = append(output,
			fmt.Sprintf("MERGE-INNER: using the control file '%s'", mergeFile),
		)
	}
	return []byte(strings.Join(output, "\n") + "\n"), nil
}

func (c *mockInstanceCmd) Output() ([]byte, error) {
	if c.pipeOut != nil || c.pipeErr != nil {
		return nil, fmt.Errorf("Output called after StdoutPipe/StderrPipe")
	}
	if err := c.Run(); err != nil {
		return nil, err
	}
	return c.getOutput()
}

func (c *mockInstanceCmd) Run() error {
	if err := c.Start(); err != nil {
		return err
	}
	return c.Wait()
}

func (c *mockInstanceCmd) Start() error {
	if !c.connector.connected {
		if err := c.connector.Connect(); err != nil {
			return err
		}
	}

	if c.running {
		return fmt.Errorf("Start called when already running")
	}
	c.running = true

	if c.pipeOut != nil {
		// Kick off a goroutine to output everything and then exit. This is a
		// goroutine because the write blocks on the consumer on the other end
		// of the pipe
		c.errCh = make(chan error, 1)
		go func() {
			defer close(c.errCh)
			defer c.pipeOut.Close()

			// Also close any stderr pipe
			if c.pipeErr != nil {
				defer c.pipeErr.Close()
			}

			output, err := c.getOutput()
			if err != nil {
				c.errCh <- err
				return
			}
			if _, err := c.pipeOut.Write(output); err != nil {
				// Suppress the ErrClosedPipe error to match the behavior of a
				// real fuzzer process, which would exit with ErrorExitCode (77
				// by default).
				if c.name == "run" && errors.Is(err, io.ErrClosedPipe) {
					err = &InstanceCmdError{ReturnCode: 77, Command: c.name, Stderr: "pipe"}
				}
				c.errCh <- err
				return
			}
		}()
	}

	// Record this command as having run
	c.connector.CmdHistory = append(c.connector.CmdHistory, c.name)

	// Record the sub-command if running `fuzz_ctl`.
	if c.name == "fuzz_ctl" {
		c.connector.FuzzCtlHistory = append(c.connector.FuzzCtlHistory, strings.Join(c.args, " "))
	}

	return nil
}

func (c *mockInstanceCmd) StdinPipe() (io.WriteCloser, error) {
	return nil, fmt.Errorf("not implemented")
}

func (c *mockInstanceCmd) StdoutPipe() (io.ReadCloser, error) {
	r, w := io.Pipe()
	c.pipeOut = w
	return r, nil
}

func (c *mockInstanceCmd) StderrPipe() (io.ReadCloser, error) {
	r, w := io.Pipe()
	c.pipeErr = w
	return r, nil
}

func (c *mockInstanceCmd) Wait() error {
	if !c.running {
		return fmt.Errorf("Wait called when not running")
	}
	c.running = false
	// TODO(fxbug.dev/45425): also check 'hasRun'

	if c.errCh != nil {
		if err := <-c.errCh; err != nil {
			return err
		}
	}

	if c.shouldFail {
		return fmt.Errorf("Intentionally broken Wait")
	}

	return nil
}

func (c *mockInstanceCmd) SetTimeout(duration time.Duration) {
	c.timeout = duration
}

func (c *mockInstanceCmd) Kill() error {
	if !c.running {
		return fmt.Errorf("Kill called when not running")
	}
	c.running = false
	return nil
}
