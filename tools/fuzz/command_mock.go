// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzz

import (
	"fmt"
	"io"
	"regexp"
	"strings"
	"time"
)

// mockInstanceCmd is the type returned by mockConnector.Command()
type mockInstanceCmd struct {
	connector *mockConnector
	name      string
	args      []string
	running   bool
	pipeOut   *io.PipeWriter
	errCh     chan error
	timeout   time.Duration
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

		// Look up the artifact prefix to use
		var artifactPrefix string
		for _, arg := range c.args[1:] {
			if parts := strings.Split(arg, "="); parts[0] == "-artifact_prefix" {
				artifactPrefix = parts[1]
				break
			}
		}
		if artifactPrefix == "" {
			return nil, fmt.Errorf("run command missing artifact_prefix option: %q", c.args)
		}
		artifactLine := fmt.Sprintf("artifact_prefix='%s'; "+
			"Test unit written to %scrash-1312", artifactPrefix, artifactPrefix)

		var output []string
		switch fuzzerName {
		case "foo/bar":
			output = []string{
				fmt.Sprintf("running %v", c.args),
				"==123==", // pid
				"MS: ",    // mut
				"Deadly signal",
				artifactLine,
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
	c.Run()
	return c.getOutput()
}

func (c *mockInstanceCmd) Run() error {
	err := c.Start()
	if err != nil {
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
			_, err = c.pipeOut.Write(output)
			if err != nil {
				c.errCh <- err
				return
			}
			err = c.pipeOut.Close()
			if err != nil {
				c.errCh <- err
				return
			}
		}()
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
			return fmt.Errorf("error in goroutine: %s", err)
		}
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
