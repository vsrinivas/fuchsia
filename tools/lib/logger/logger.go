// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Package logger provides methods for logging with different levels.
package logger

import (
	"context"
	"fmt"
	"io"
	goLog "log"
	"os"

	"go.fuchsia.dev/fuchsia/tools/lib/color"
)

type globalLoggerKeyType struct{}

// WithLogger returns the context with its logger set as the provided Logger.
func WithLogger(ctx context.Context, logger *Logger) context.Context {
	return context.WithValue(ctx, globalLoggerKeyType{}, logger)
}

// Logger represents a specific LogLevel with a specified color and prefix.
type Logger struct {
	LoggerLevel   LogLevel
	goLogger      *goLog.Logger
	goErrorLogger *goLog.Logger
	color         color.Color
	prefix        string
}

// LogLevel represents different levels for logging depending on the amount of detail wanted.
type LogLevel int

const (
	NoLogLevel LogLevel = iota
	FatalLevel
	ErrorLevel
	WarningLevel
	InfoLevel
	DebugLevel
	TraceLevel
)

// String returns the string representation of the LogLevel.
func (l *LogLevel) String() string {
	switch *l {
	case NoLogLevel:
		return "no"
	case FatalLevel:
		return "fatal"
	case ErrorLevel:
		return "error"
	case WarningLevel:
		return "warning"
	case InfoLevel:
		return "info"
	case DebugLevel:
		return "debug"
	case TraceLevel:
		return "trace"
	}
	return ""
}

// Set sets the LogLevel based on its string value.
func (l *LogLevel) Set(s string) error {
	switch s {
	case "fatal":
		*l = FatalLevel
	case "error":
		*l = ErrorLevel
	case "warning":
		*l = WarningLevel
	case "info":
		*l = InfoLevel
	case "debug":
		*l = DebugLevel
	case "trace":
		*l = TraceLevel
	default:
		return fmt.Errorf("%s is not a valid level", s)
	}
	return nil
}

// NewLogger creates a new logger instance. The loggerLevel variable sets the log level for the logger.
// The color variable specifies the visual color of displayed log output.
// The outWriter and errWriter variables set the destination to which non-error and error data will be written.
// The prefix appears on the same line directly preceding any log data.
func NewLogger(loggerLevel LogLevel, color color.Color, outWriter, errWriter io.Writer, prefix string) *Logger {
	if outWriter == nil {
		outWriter = os.Stdout
	}
	if errWriter == nil {
		errWriter = os.Stderr
	}
	l := &Logger{
		LoggerLevel:   loggerLevel,
		goLogger:      goLog.New(outWriter, "", goLog.Ldate|goLog.Lmicroseconds),
		goErrorLogger: goLog.New(errWriter, "", goLog.Ldate|goLog.Lmicroseconds),
		color:         color,
		prefix:        prefix,
	}
	return l
}

func (l *Logger) log(prefix, format string, a ...interface{}) {
	l.goLogger.Printf("%s%s%s", l.prefix, prefix, fmt.Sprintf(format, a...))
}

// Logf logs the string based on the loglevel of the string and the LogLevel of the logger.
func (l *Logger) Logf(loglevel LogLevel, format string, a ...interface{}) {
	switch loglevel {
	case InfoLevel:
		l.Infof(format, a...)
	case DebugLevel:
		l.Debugf(format, a...)
	case TraceLevel:
		l.Tracef(format, a...)
	case WarningLevel:
		l.Warningf(format, a...)
	case ErrorLevel:
		l.Errorf(format, a...)
	case FatalLevel:
		l.Fatalf(format, a...)
	default:
		panic(fmt.Sprintf("Undefined loglevel: %v, log message: %s", loglevel, fmt.Sprintf(format, a...)))
	}
}

func Logf(ctx context.Context, logLevel LogLevel, format string, a ...interface{}) {
	if v, ok := ctx.Value(globalLoggerKeyType{}).(*Logger); ok && v != nil {
		v.Logf(logLevel, format, a...)
	} else {
		goLog.Printf(format, a...)
	}
}

// Infof logs the string if the logger is at least InfoLevel.
func (l *Logger) Infof(format string, a ...interface{}) {
	if l.LoggerLevel >= InfoLevel {
		l.log("", format, a...)
	}
}

func Infof(ctx context.Context, format string, a ...interface{}) {
	Logf(ctx, InfoLevel, format, a...)
}

// Debugf logs the string if the logger is at least DebugLevel.
func (l *Logger) Debugf(format string, a ...interface{}) {
	if l.LoggerLevel >= DebugLevel {
		l.log(l.color.Cyan("DEBUG: "), format, a...)
	}
}

func Debugf(ctx context.Context, format string, a ...interface{}) {
	Logf(ctx, DebugLevel, format, a...)
}

// Tracef logs the string if the logger is at least TraceLevel.
func (l *Logger) Tracef(format string, a ...interface{}) {
	if l.LoggerLevel >= TraceLevel {
		l.log(l.color.Blue("TRACE: "), format, a...)
	}
}

func Tracef(ctx context.Context, format string, a ...interface{}) {
	Logf(ctx, TraceLevel, format, a...)
}

// Warningf logs the string if the logger is at least WarningLevel.
func (l *Logger) Warningf(format string, a ...interface{}) {
	if l.LoggerLevel >= WarningLevel {
		l.log(l.color.Yellow("WARN: "), format, a...)
	}
}

func Warningf(ctx context.Context, format string, a ...interface{}) {
	Logf(ctx, WarningLevel, format, a...)
}

// Errorf logs the string if the logger is at least ErrorLevel.
func (l *Logger) Errorf(format string, a ...interface{}) {
	if l.LoggerLevel >= ErrorLevel {
		l.goErrorLogger.Printf("%s%s%s", l.prefix, l.color.Red("ERROR: "), fmt.Sprintf(format, a...))
	}
}

func Errorf(ctx context.Context, format string, a ...interface{}) {
	Logf(ctx, ErrorLevel, format, a...)
}

// Fatalf logs the string if the logger is at least FatalLevel.
func (l *Logger) Fatalf(format string, a ...interface{}) {
	if l.LoggerLevel >= FatalLevel {
		l.goErrorLogger.Fatalf("%s%s%s", l.prefix, l.color.Red("FATAL: "), fmt.Sprintf(format, a...))
	}
}

func Fatalf(ctx context.Context, format string, a ...interface{}) {
	Logf(ctx, FatalLevel, format, a...)
}
