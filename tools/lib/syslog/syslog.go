// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package syslog

import (
	"context"
	"io"
	"time"

	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/lib/retry"
	"go.fuchsia.dev/fuchsia/tools/net/sshutil"
)

const (
	// The program on fuchsia used to stream system logs through a shell, not to
	// be confused with the zircon host tool "loglistener" (no underscore) used
	// to stream zircon-level logs to host.
	logListener = "/bin/log_listener"

	// Time to wait between attempts to reconnect after losing the connection.
	defaultReconnectInterval = 5 * time.Second
)

// Syslogger streams systems logs from a Fuchsia instance.
type Syslogger struct {
	client sshClient
}

type sshClient interface {
	Run(context.Context, []string, io.Writer, io.Writer) error
	ReconnectWithBackoff(context.Context, retry.Backoff) error
	Close()
}

// NewSyslogger creates a new Syslogger, given an SSH session with a Fuchsia instance.
func NewSyslogger(client *sshutil.Client) *Syslogger {
	return &Syslogger{
		client: client,
	}
}

// Stream writes system logs to a given writer; it blocks until the stream is
// is terminated or a Done is signaled. The syslogger streams from the very
// beggining of the system's uptime.
func (s *Syslogger) Stream(ctx context.Context, output io.Writer) error {
	cmd := []string{logListener}
	for {
		// Note: Fuchsia's log_listener does not write to stderr.
		err := s.client.Run(ctx, cmd, output, nil)

		completedSuccessfully := err == nil || ctx.Err() != nil
		if !completedSuccessfully {
			logger.Debugf(ctx, "error streaming syslog: %v", err)
		}

		// We need not attempt to reconnect if we hit an error unrelated to the
		// connection (which is probably unrecoverable) or if the context was
		// canceled (context cancellation is the only mechanism for stopping
		// this method, so it generally indicates that the caller is exiting
		// normally).
		if completedSuccessfully || !sshutil.IsConnectionError(err) {
			logger.Debugf(ctx, "syslog streaming complete")
			return err
		}

		logger.Errorf(ctx, "syslog: SSH client unresponsive; will attempt to reconnect and continue streaming: %v", err)
		if err := s.client.ReconnectWithBackoff(ctx, retry.NewConstantBackoff(defaultReconnectInterval)); err != nil {
			// The context probably got cancelled before we were able to
			// reconnect.
			return err
		}
		logger.Infof(ctx, "syslog: refreshed ssh connection")
		io.WriteString(output, "\n\n<< SYSLOG STREAM INTERRUPTED; RECONNECTING NOW >>\n\n")
	}
}
