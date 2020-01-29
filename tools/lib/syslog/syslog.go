// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package syslog

import (
	"context"
	"fmt"
	"io"

	"golang.org/x/crypto/ssh"

	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/lib/runner"
	"go.fuchsia.dev/fuchsia/tools/net/sshutil"
)

// The program on fuchsia used to stream system logs through a shell, not to be confused
// with the zircon host tool "loglistener" (no underscore) used to stream zircon-level logs to host.
const logListener = "/bin/log_listener"

// Syslogger streams systems logs from a Fuchsia instance.
type Syslogger struct {
	r sshRunner
}

type sshRunner interface {
	Run(context.Context, []string, io.Writer, io.Writer) error
	Reconnect(context.Context) error
	Close() error
}

// NewSyslogger creates a new Syslogger, given an SSH session with a Fuchsia instance.
func NewSyslogger(client *ssh.Client, config *ssh.ClientConfig) *Syslogger {
	return &Syslogger{r: runner.NewSSHRunner(client, config)}
}

// Stream writes system logs to a given writer; it blocks until the stream is
// is terminated or a Done is signaled. The default behavior of the syslogger
// is to stream from the very beggining of the system's uptime; if
// |sinceNow| is true, then the logs will truncated to begin at the time of execution.
func (s *Syslogger) Stream(ctx context.Context, output io.Writer, sinceNow bool) error {
	for {
		cmd := []string{logListener}
		if sinceNow {
			cmd = append(cmd, "--since_now", "yes")
		}

		// Note: Fuchsia's log_listener does not write to stderr.
		err := s.r.Run(ctx, cmd, output, nil)
		// We need not attempt to reconnect if the context was canceled or if we
		// hit an error unrelated to the connection.
		if err == nil || ctx.Err() != nil || !sshutil.IsConnectionError(err) {
			return err
		}
		logger.Errorf(ctx, "syslog: SSH client unresponsive; will attempt to reconnect and continue streaming: %v", err)
		if err := s.r.Reconnect(ctx); err != nil {
			err = fmt.Errorf("syslog: failed to refresh SSH client: %v", err)
			return err
		}
		io.WriteString(output, "\n\n<< SYSLOG STREAM INTERRUPTED; RECONNECTING NOW >>\n\n")

		// Start streaming from this point in time onward in the case of connection loss.
		sinceNow = true
	}
}

// Close tidies up the system logging session with the corresponding fuchsia instance.
func (s *Syslogger) Close() error {
	return s.r.Close()
}
