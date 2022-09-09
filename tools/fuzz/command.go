// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzz

import (
	"bytes"
	"fmt"
	"io"
	"time"

	"github.com/golang/glog"
	"golang.org/x/crypto/ssh"
)

// An InstanceCmd represents a remote command to be run on an Instance
// This interface is a superset of os.exec.Cmd
// TODO(fxbug.dev/45424): use timeout for all methods
type InstanceCmd interface {
	// Output runs the command and returns its combined output.
	Output() ([]byte, error)

	// Runs the specified command and waits for it to complete.
	//
	// The returned error is nil if the command runs, has no problems copying
	// input and output, and exits with a zero exit status.
	Run() error

	// Start starts the specified command but does not wait for it to complete.
	Start() error

	// StdinPipe returns a pipe that will be connected to the command's input
	// when the command starts. The pipe will be closed automatically after
	// Wait sees the command exit. A caller need only call Close to force the
	// pipe to close sooner. For example, if the command being run will not
	// exit until the input is closed, the caller must close the pipe.
	//
	// If used, this must be called before Start() or Run().
	StdinPipe() (io.WriteCloser, error)

	// StdoutPipe returns a pipe that will be connected to the command's stdout
	// when the command starts.
	//
	// Wait will close the pipe after seeing the command exit, so most callers
	// need not close the pipe themselves; however, an implication is that it
	// is incorrect to call Wait before all reads from the pipe have completed.
	// For the same reason, it is incorrect to call Run when using StdoutPipe.
	//
	// If used, this must be called before Start() or Run().
	StdoutPipe() (io.ReadCloser, error)

	// Same as StdoutPipe but for stderr
	//
	// If used, this must be called before Start() or Run().
	StderrPipe() (io.ReadCloser, error)

	// Wait waits for the command to exit and waits for any copying
	// to input or from output to complete.
	//
	// The command must have been started by Start.
	//
	// The returned error is nil if the command runs, has no problems
	// copying input and output, and exits with a zero exit status
	// before any timeout set with SetTimeout is triggered.
	//
	// Wait releases any resources associated with the InstanceCmd.
	Wait() error

	// The following methods are *not* included in exec.Cmd:

	// SetTimeout sets a timeout that will be used for all blocking
	// methods. If a method takes longer than the timeout duration to
	// complete, it will return early with an error.  If not set, or
	// set to 0, methods will block forever.
	SetTimeout(duration time.Duration)

	// Kill forcefully terminates the remote command
	Kill() error
}

// A SSHInstanceCmd represents a command to be run over SSH
type SSHInstanceCmd struct {
	connector *SSHConnector
	cmdline   string
	pid       int

	errlog  bytes.Buffer
	session *ssh.Session
	// true if StderrPipe has been called
	stderrpipe bool

	timeout time.Duration
}

// Output executes the command and returns its output
func (c *SSHInstanceCmd) Output() ([]byte, error) {
	if err := c.initialize(); err != nil {
		return nil, err
	}

	var buf bytes.Buffer
	c.session.Stdout = &buf
	c.session.Stderr = &buf
	if err := c.Run(); err != nil {
		return nil, err
	}
	return buf.Bytes(), nil
}

// Run runs the command to completion
func (c *SSHInstanceCmd) Run() error {
	if err := c.Start(); err != nil {
		return err
	}

	return c.Wait()
}

// Initialize the underlying SSH Session object, if necessary.
func (c *SSHInstanceCmd) initialize() error {
	if c.session != nil {
		return nil
	}

	if c.connector.client == nil {
		if err := c.connector.Connect(); err != nil {
			return err
		}
	}

	session, err := c.connector.client.NewSession()
	if err != nil {
		return fmt.Errorf("error starting ssh session: %s", err)
	}

	c.session = session

	return nil
}

// Start the command, but don't wait for it to complete
func (c *SSHInstanceCmd) Start() error {
	if c.pid != 0 {
		return fmt.Errorf("start called on already-running command")
	}

	if err := c.initialize(); err != nil {
		return err
	}

	glog.Infof("Running ssh command: %s", c.cmdline)

	// Log stderr in case the command fails.
	// TODO(fxbug.dev/45424): limit size of errlog, we only really want the
	// tail for debugging
	if !c.stderrpipe {
		if c.session.Stderr == nil {
			c.session.Stderr = &c.errlog
		} else {
			c.session.Stderr = io.MultiWriter(c.session.Stderr, &c.errlog)
		}
	}

	if err := c.session.Start(c.cmdline); err != nil {
		return err
	}

	return nil
}

// The StdoutPipe and StderrPipe methods for ssh.Session diverge slightly from
// exec.Cmd in that they return Readers instead of ReadClosers, so closing the
// read end of the pipe cannot be used propagate errors backwards in a series
// of chained commands. We work around this here by implementing a Close method
// that calls Kill on the command.
//
// Note: We *also* can't configure Stdout/Stderr directly with our own Pipes
// because in that case EOFs are not propagated to the pipes when the remote
// command exits.
type KillCloser struct {
	io.Reader
	cmd *SSHInstanceCmd
}

func (kc *KillCloser) Close() error {
	return kc.cmd.Kill()
}

// StdinPipe returns a pipe connected to the command's input
func (c *SSHInstanceCmd) StdinPipe() (io.WriteCloser, error) {
	if err := c.initialize(); err != nil {
		return nil, err
	}

	return c.session.StdinPipe()
}

// StdoutPipe returns a pipe connected to the command's stdout
func (c *SSHInstanceCmd) StdoutPipe() (io.ReadCloser, error) {
	if err := c.initialize(); err != nil {
		return nil, err
	}

	r, err := c.session.StdoutPipe()
	if err != nil {
		return nil, err
	}
	return &KillCloser{Reader: r, cmd: c}, nil
}

// StderrPipe returns a pipe connected to the command's stderr
func (c *SSHInstanceCmd) StderrPipe() (io.ReadCloser, error) {
	if err := c.initialize(); err != nil {
		return nil, err
	}

	r, err := c.session.StderrPipe()
	if err != nil {
		return nil, err
	}

	c.stderrpipe = true
	return &KillCloser{Reader: r, cmd: c}, nil
}

// Kill sends a KILL signal to the remote process
func (c *SSHInstanceCmd) Kill() error {
	return c.session.Signal(ssh.SIGKILL)
}

// SetTimeout sets the global timeout
func (c *SSHInstanceCmd) SetTimeout(duration time.Duration) {
	c.timeout = duration
}

// Wait for the remote command to complete
func (c *SSHInstanceCmd) Wait() error {
	defer c.session.Close()

	errs := make(chan error)
	go func() { errs <- c.session.Wait() }()

	var timeoutCh <-chan time.Time
	if c.timeout != 0 {
		timeoutCh = time.After(c.timeout)
	}

	select {
	case err := <-errs:
		if err == nil {
			return nil
		}

		if cmderr, ok := err.(*ssh.ExitError); ok {
			return &InstanceCmdError{
				ReturnCode: cmderr.ExitStatus(),
				Command:    c.cmdline,
				Stderr:     c.errlog.String(),
			}
		}

		return err
	case <-timeoutCh:
		// TODO(fxbug.dev/45424): clean up the ssh command
		return fmt.Errorf("timeout waiting for command to complete")
	}
}

// InstanceCmdError includes extra information when commands fail
type InstanceCmdError struct {
	ReturnCode int
	Command    string
	Stderr     string
}

func (e *InstanceCmdError) Error() string {
	return fmt.Sprintf("'%s' exited with error: %d (%s)", e.Command, e.ReturnCode, e.Stderr)
}
