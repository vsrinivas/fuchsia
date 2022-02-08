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
	errCh      chan error
	timeout    time.Duration
	shouldFail bool
}

func (c *mockInstanceCmd) getOutput() ([]byte, error) {
	switch c.name {
	case "run":
		re := regexp.MustCompile(`fuchsia-pkg://fuchsia\.com/([^#]+)#meta/([^\.]+)\.cmx`)
		m := re.FindStringSubmatch(c.args[0])
		if m == nil {
			return nil, fmt.Errorf("unexpected run argument: %q", c.args[0])
		}
		fuzzerName := fmt.Sprintf("%s/%s", m[1], m[2])

		// Look up arguments that we want to test
		var artifactPrefix string
		var mergeFile string
		var corpusPath string
		for _, arg := range c.args[1:] {
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
			return nil, fmt.Errorf("run command missing artifact_prefix option: %q", c.args)
		}
		artifactLine := fmt.Sprintf("artifact_prefix='%s'; "+
			"Test unit written to %scrash-1312", artifactPrefix, artifactPrefix)

		if corpusPath == "" {
			return nil, fmt.Errorf("run command missing output corpus dir: %q", c.args)
		}
		corpusLine := fmt.Sprintf("INFO:        4 files found in %s", corpusPath)

		var output []string
		switch fuzzerName {
		case "foo/bar":
			// Create 64k of filler to ensure we saturate any pipes on the
			// output that aren't being properly serviced.
			filler := make([]string, 1024)
			for j := 0; j < 1024; j++ {
				filler[j] = strings.Repeat("data", 64/4)
			}
			output = append(filler,
				fmt.Sprintf("running %v", c.args),
				corpusLine,
				"Running: "+corpusPath+"/testcase",
				"==123==", // pid
				"MS: ",    // mut
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
		case "fail/nopid":
			// No PID
			output = []string{
				fmt.Sprintf("running %v", c.args),
				"MS: ", // mut
				"Deadly signal",
				artifactLine,
			}
		case "fail/notfound":
			return nil, &InstanceCmdError{ReturnCode: 127, Command: c.name, Stderr: "not found"}
		default:
			output = []string{fmt.Sprintf("unknown fuzzer %q", fuzzerName)}
		}
		return []byte(strings.Join(output, "\n") + "\n"), nil
	default:
		return nil, fmt.Errorf("unknown command: %q", c.name)
	}
}

func (c *mockInstanceCmd) Output() ([]byte, error) {
	if c.pipeOut != nil {
		return nil, fmt.Errorf("Output called after StdoutPipe")
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
			if err := c.pipeOut.Close(); err != nil {
				c.errCh <- err
				return
			}
		}()
	}

	// Record this command as having run
	c.connector.CmdHistory = append(c.connector.CmdHistory, c.name)

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

func (c *mockInstanceCmd) Wait() error {
	// On quit, close stdout in case someone is blocking on us
	if c.pipeOut != nil {
		defer c.pipeOut.Close()
	}

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
