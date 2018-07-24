// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package lg

import (
	"fmt"
	"os"
)

type TeeLogger struct {
	loggers []Logger
}

func NewTeeLogger(loggers ...Logger) *TeeLogger {
	return &TeeLogger{loggers: loggers}
}

func (l *TeeLogger) Infof(format string, v ...interface{}) error {
	return l.Output(1, InfoLevel, fmt.Sprintf(format, v...))
}

func (l *TeeLogger) Warnf(format string, v ...interface{}) error {
	return l.Output(1, WarningLevel, fmt.Sprintf(format, v...))
}

func (l *TeeLogger) Errorf(format string, v ...interface{}) error {
	return l.Output(1, ErrorLevel, fmt.Sprintf(format, v...))
}

func (l *TeeLogger) Fatalf(format string, v ...interface{}) {
	l.Output(1, FatalLevel, fmt.Sprintf(format, v...))
	os.Exit(1)
}

func (l *TeeLogger) Output(callDepth int, logLevel LogLevel, s string) error {
	for _, logger := range l.loggers {
		if err := logger.Output(callDepth+1, logLevel, s); err != nil {
			return err
		}
	}

	return nil
}
