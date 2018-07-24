// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package lg

import (
	"fmt"
	"log"
	"os"
	"sync"
)

type LogLevel int32

const (
	InfoLevel LogLevel = iota
	WarningLevel
	ErrorLevel
	FatalLevel
)

func (ll LogLevel) String() string {
	switch ll {
	case InfoLevel:
		return "INFO"
	case WarningLevel:
		return "WARNING"
	case ErrorLevel:
		return "ERROR"
	case FatalLevel:
		return "FATAL"
	default:
		if int(ll) < 0 {
			return fmt.Sprintf("VLOG(%d)", -(int(ll)))
		}
		return "INVALID"
	}
}

type Logger interface {
	Infof(format string, v ...interface{}) error

	Warnf(format string, v ...interface{}) error

	Errorf(format string, v ...interface{}) error

	Fatalf(format string, v ...interface{})

	Output(callDepth int, logLevel LogLevel, s string) error
}

type defaultLogger struct {
	mu     sync.Mutex
	logger Logger
}

var std = defaultLogger{
	logger: NewStdLogger(log.New(os.Stderr, "", log.LstdFlags)),
}

func GetDefaultLogger() Logger {
	return std.logger
}

func SetDefaultLogger(l Logger) {
	std.mu.Lock()
	defer std.mu.Unlock()
	std.logger = l
}

func Infof(format string, v ...interface{}) error {
	return std.logger.Infof(format, v...)
}

func Warnf(format string, v ...interface{}) error {
	return std.logger.Warnf(format, v...)
}

func Errorf(format string, v ...interface{}) error {
	return std.logger.Errorf(format, v...)
}

func Fatalf(format string, v ...interface{}) {
	std.logger.Fatalf(format, v...)
}
