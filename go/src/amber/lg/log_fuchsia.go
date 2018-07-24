// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build fuchsia

package lg

import (
	"app/context"
	"fmt"
	"os"
	syslog "syslog/logger"
)

type FuchsiaLogger struct {
	inner *syslog.Logger
}

func NewFuchsiaLogger(c *context.Connector) (*FuchsiaLogger, error) {
	options := syslog.GetDefaultInitOptions()
	options.Connector = c

	return NewFuchsiaWithConfig(options)
}

func NewFuchsiaLoggerWithTags(c *context.Connector, tags ...string) (*FuchsiaLogger, error) {
	options := syslog.GetDefaultInitOptions()
	options.Tags = tags
	options.Connector = c

	return NewFuchsiaWithConfig(options)
}

func NewFuchsiaWithConfig(options syslog.LogInitOptions) (*FuchsiaLogger, error) {
	l, err := syslog.NewLogger(options)
	if err != nil {
		return nil, err
	}

	return &FuchsiaLogger{inner: l}, nil
}

func (l *FuchsiaLogger) Infof(format string, v ...interface{}) error {
	return l.Output(1, InfoLevel, fmt.Sprintf(format, v...))
}

func (l *FuchsiaLogger) Warnf(format string, v ...interface{}) error {
	return l.Output(1, WarningLevel, fmt.Sprintf(format, v...))
}

func (l *FuchsiaLogger) Errorf(format string, v ...interface{}) error {
	return l.Output(1, ErrorLevel, fmt.Sprintf(format, v...))
}

func (l *FuchsiaLogger) Fatalf(format string, v ...interface{}) {
	l.Output(1, FatalLevel, fmt.Sprintf(format, v...))
	os.Exit(1)
}

func (l *FuchsiaLogger) Output(callLevel int, logLevel LogLevel, s string) error {
	// Strip out the trailing newline the `log` library adds because the
	// syslog service also adds a trailing newline.
	if len(s) > 0 && s[len(s)-1] == '\n' {
		s = s[:len(s)-1]
	}

	return l.inner.VLogf(int(logLevel), s)
}
