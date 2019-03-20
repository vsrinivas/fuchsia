// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package botanist

import (
	"context"
	"io"

	"golang.org/x/crypto/ssh"

	"fuchsia.googlesource.com/tools/runner"
)

// The program on fuchsia used to stream system logs through a shell, not to be confused
// with the zircon host tool "loglistener" (no underscore) used to zircon-level logs to host.
const logListener = "bin/log_listener"

// Syslogger represents an session with a Fuchsia instance through which system logs may be streamed.
type Syslogger struct {
	r *runner.SSHRunner
}

// NewSyslogger creates a new Syslogger, given an SSH session with a Fuchsia instance.
func NewSyslogger(client *ssh.Client) (*Syslogger, error) {
	session, err := client.NewSession()
	if err != nil {
		return nil, err
	}
	return &Syslogger{r: &runner.SSHRunner{Session: session}}, nil
}

// Stream writes system logs to a given writer; it blocks until the stream is
// is terminated or a Done is signaled.
func (s *Syslogger) Stream(ctx context.Context, output io.Writer) error {
	// Note: Fuchsia's log_listener does not write to stderr.
	return s.r.Run(ctx, []string{logListener}, output, nil)
}

// Close tidies up the system logging session with the corresponding fuchsia instance.
func (s *Syslogger) Close() error {
	s.r.Session.Signal(ssh.SIGKILL)
	return s.r.Session.Close()
}
