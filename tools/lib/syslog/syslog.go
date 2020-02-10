// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package syslog

import (
	"context"
	"errors"
	"io"
	"time"

	"golang.org/x/crypto/ssh"

	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/lib/retry"
	"go.fuchsia.dev/fuchsia/tools/lib/runner"
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
	r                 sshRunner
	reconnectInterval time.Duration
}

type sshRunner interface {
	Run(context.Context, []string, io.Writer, io.Writer) error
	Reconnect(context.Context) (*ssh.Client, error)
	Close() error
}

// NewSyslogger creates a new Syslogger, given an SSH session with a Fuchsia instance.
func NewSyslogger(client *ssh.Client, config *ssh.ClientConfig) *Syslogger {
	return &Syslogger{
		r:                 runner.NewSSHRunner(client, config),
		reconnectInterval: defaultReconnectInterval,
	}
}

// Stream writes system logs to a given writer; it blocks until the stream is
// is terminated or a Done is signaled. The syslogger streams from the very
// beggining of the system's uptime.
func (s *Syslogger) Stream(ctx context.Context, output io.Writer) error {
	for {
		cmd := []string{logListener}
		// Note: Fuchsia's log_listener does not write to stderr.
		err := s.r.Run(ctx, cmd, output, nil)
		// We need not attempt to reconnect if the context was canceled or if we
		// hit an error unrelated to the connection.
		if err == nil || ctx.Err() != nil || !errors.Is(err, sshutil.ConnectionError) {
			return err
		}
		logger.Errorf(ctx, "syslog: SSH client unresponsive; will attempt to reconnect and continue streaming: %v", err)
		err = retry.Retry(ctx, retry.NewConstantBackoff(s.reconnectInterval), func() error {
			_, err := s.r.Reconnect(ctx)
			if err != nil {
				logger.Debugf(ctx, "syslog: failed to refresh SSH client, will try again after %s: %v", s.reconnectInterval, err)
			} else {
				logger.Infof(ctx, "syslog: refreshed ssh connection")
			}
			return err
		}, nil)
		if err != nil {
			// The context probably got cancelled before we were able to
			// reconnect.
			return err
		}
		io.WriteString(output, "\n\n<< SYSLOG STREAM INTERRUPTED; RECONNECTING NOW >>\n\n")
	}
}

// Close tidies up the system logging session with the corresponding fuchsia instance.
func (s *Syslogger) Close() error {
	return s.r.Close()
}
