// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package lg

import (
	"fmt"
	"log"
	"os"
)

type StdLogger struct {
	logger *log.Logger
}

func NewStdLogger(logger *log.Logger) *StdLogger {
	return &StdLogger{logger: logger}
}

func (l *StdLogger) Infof(format string, v ...interface{}) error {
	return l.Output(1, InfoLevel, fmt.Sprintf(format, v...))
}

func (l *StdLogger) Warnf(format string, v ...interface{}) error {
	return l.Output(1, WarningLevel, fmt.Sprintf(format, v...))
}

func (l *StdLogger) Errorf(format string, v ...interface{}) error {
	return l.Output(1, ErrorLevel, fmt.Sprintf(format, v...))
}

func (l *StdLogger) Fatalf(format string, v ...interface{}) {
	l.Output(1, FatalLevel, fmt.Sprintf(format, v...))
	os.Exit(1)
}

func (l *StdLogger) Output(callDepth int, logLevel LogLevel, s string) error {
	l.logger.Output(callDepth+1, s)
	return nil
}
