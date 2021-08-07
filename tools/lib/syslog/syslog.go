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
	"go.fuchsia.dev/fuchsia/tools/lib/syslog/constants"
	"go.fuchsia.dev/fuchsia/tools/net/sshutil"
)

const (
	// The program on fuchsia used to stream system logs through a shell, not to
	// be confused with the zircon host tool "loglistener" (no underscore) used
	// to stream zircon-level logs to host.
	LogListener = "/bin/log_listener"

	// Time to wait between attempts to reconnect after losing the connection.
	defaultReconnectInterval = 5 * time.Second
)

// Syslogger streams systems logs from a Fuchsia instance.
type Syslogger struct {
	client  sshClient
	running bool
}

type sshClient interface {
	Run(context.Context, []string, io.Writer, io.Writer) error
	ReconnectWithBackoff(context.Context, retry.Backoff) error
}

// NewSyslogger creates a new Syslogger, given an SSH session with a Fuchsia instance.
func NewSyslogger(client *sshutil.Client) *Syslogger {
	return &Syslogger{
		client: client,
	}
}

// IsRunning returns whether the syslog is still streaming. Once the goroutine
// started in Stream() completes, this should return false.
func (s *Syslogger) IsRunning() bool {
	return s.running
}

// Stream writes system logs to a given writer, starting from the very beginning
// of the system's uptime. It keeps running until the context is canceled or until an
// unexpected (not SSH-related) error occurs; otherwise, it returns errors to
// the returned channel to signify the syslog was interrupted and restarted.
func (s *Syslogger) Stream(ctx context.Context, output io.Writer) <-chan error {
	errs := make(chan error, 1)
	sendErr := func(errs chan error, err error) {
		select {
		case <-errs:
			// Clear the channel if nobody is listening.
		default:
			// Break out if channel is empty.
		}
		errs <- err
	}
	cmd := []string{LogListener}
	s.running = true
	go func() {
		for {
			// Note: Fuchsia's log_listener does not write to stderr.
			err := s.client.Run(ctx, cmd, output, nil)
			if ctx.Err() == nil {
				if err != nil {
					logger.Debugf(ctx, "error streaming syslog: %s", err)
				} else {
					logger.Debugf(ctx, "log_listener exited successfully, will rerun")
					// Don't stream from the beginning of the system's uptime, since
					// that would include logs that we've already streamed.
					cmd = []string{LogListener, "--since_now", "yes"}
					continue
				}
			}

			// We need not attempt to reconnect if we hit an error unrelated to the
			// connection (which is probably unrecoverable) or if the context was
			// canceled (context cancellation is the only mechanism for stopping
			// this method, so it generally indicates that the caller is exiting
			// normally).
			if ctx.Err() != nil || !sshutil.IsConnectionError(err) {
				logger.Debugf(ctx, "syslog streaming complete: %s", err)
				s.running = false
				sendErr(errs, err)
				close(errs)
				return
			}

			logger.Errorf(ctx, "syslog: SSH client unresponsive; will attempt to reconnect and continue streaming: %s", err)
			if err := s.client.ReconnectWithBackoff(ctx, retry.NewConstantBackoff(defaultReconnectInterval)); err != nil {
				// The context probably got cancelled before we were able to
				// reconnect.
				if ctx.Err() != nil {
					logger.Errorf(ctx, "syslog: %s: %s", constants.CtxReconnectError, ctx.Err())
				}
				s.running = false
				sendErr(errs, err)
				close(errs)
				return
			}
			// Start streaming from the beginning of the system's uptime again now that
			// we're rebooting.
			cmd = []string{LogListener}
			logger.Infof(ctx, "syslog: refreshed ssh connection")
			io.WriteString(output, "\n\n<< SYSLOG STREAM INTERRUPTED; RECONNECTING NOW >>\n\n")
			sendErr(errs, err)
		}
	}()
	return errs
}
