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

// LoggerFromContext returns the context logger if configured, otherwise nil.
func LoggerFromContext(ctx context.Context) *Logger {
	if v, ok := ctx.Value(globalLoggerKeyType{}).(*Logger); ok && v != nil {
		return v
	}
	return nil
}

// Logger represents a specific LogLevel with a specified color and prefix.
type Logger struct {
	LoggerLevel   LogLevel
	goLogger      *goLog.Logger
	goErrorLogger *goLog.Logger
	color         color.Color
	prefix        interface{}
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

var levelToName = map[LogLevel]string{
	NoLogLevel:   "no",
	FatalLevel:   "fatal",
	ErrorLevel:   "error",
	WarningLevel: "warning",
	InfoLevel:    "info",
	DebugLevel:   "debug",
	TraceLevel:   "trace",
}

// Populated at runtime by init() by inverting levelToName.
var nameToLevel = map[string]LogLevel{}

func init() {
	for level, name := range levelToName {
		nameToLevel[name] = level
	}
}

// Copied from Go log so callers don't need to also import log.
const (
	Ldate         = 1 << iota             // the date in the local time zone: 2009/01/23
	Ltime                                 // the time in the local time zone: 01:23:23
	Lmicroseconds                         // microsecond resolution: 01:23:23.123123.  assumes Ltime.
	Llongfile                             // full file name and line number: /a/b/c/d.go:23
	Lshortfile                            // final file name element and line number: d.go:23. overrides Llongfile
	LUTC                                  // if Ldate or Ltime is set, use UTC rather than the local time zone
	Lmsgprefix                            // move the "prefix" from the beginning of the line to before the message
	LstdFlags     = Ldate | Lmicroseconds // initial values for the standard logger
)

// StartDepth defines the starting point for the call depth when entering the logger.
const startDepth = 2

// String returns the string representation of the LogLevel, or an empty string
// if the LogLevel has no string representation specified.
func (l *LogLevel) String() string {
	return levelToName[*l]
}

// Set sets the LogLevel based on its string value.
func (l *LogLevel) Set(s string) error {
	level, ok := nameToLevel[s]
	if !ok {
		return fmt.Errorf("%s is not a valid level", s)
	}
	*l = level
	return nil
}

// NewLogger creates a new logger instance. The loggerLevel variable sets the log level for the logger.
// The color variable specifies the visual color of displayed log output.
// The outWriter and errWriter variables set the destination to which non-error and error data will be written.
// The prefix appears on the same line directly preceding any log data. It should be thread-safe to format this value.
func NewLogger(loggerLevel LogLevel, color color.Color, outWriter, errWriter io.Writer, prefix interface{}) *Logger {
	if outWriter == nil {
		outWriter = os.Stdout
	}
	if errWriter == nil {
		errWriter = os.Stderr
	}
	l := &Logger{
		LoggerLevel:   loggerLevel,
		goLogger:      goLog.New(outWriter, "", LstdFlags),
		goErrorLogger: goLog.New(errWriter, "", LstdFlags),
		color:         color,
		prefix:        prefix,
	}
	return l
}

func (l *Logger) SetFlags(flags int) {
	l.goLogger.SetFlags(flags)
	l.goErrorLogger.SetFlags(flags)
}

func (l *Logger) log(callDepth int, prefix, format string, a ...interface{}) {
	l.goLogger.Output(callDepth+1, fmt.Sprintf("%s%s%s", l.prefix, prefix, fmt.Sprintf(format, a...)))
}

// Logf logs the string based on the loglevel of the string and the LogLevel of the logger.
func (l *Logger) logf(callDepth int, loglevel LogLevel, format string, a ...interface{}) {
	switch loglevel {
	case InfoLevel:
		l.infof(callDepth+1, format, a...)
	case DebugLevel:
		l.debugf(callDepth+1, format, a...)
	case TraceLevel:
		l.tracef(callDepth+1, format, a...)
	case WarningLevel:
		l.warningf(callDepth+1, format, a...)
	case ErrorLevel:
		l.errorf(callDepth+1, format, a...)
	case FatalLevel:
		l.fatalf(callDepth+1, format, a...)
	default:
		panic(fmt.Sprintf("Undefined loglevel: %v, log message: %s", loglevel, fmt.Sprintf(format, a...)))
	}
}

func Logf(ctx context.Context, logLevel LogLevel, format string, a ...interface{}) {
	logf(startDepth, ctx, logLevel, format, a...)
}

func logf(callDepth int, ctx context.Context, logLevel LogLevel, format string, a ...interface{}) {
	if v := LoggerFromContext(ctx); v != nil {
		v.logf(callDepth+1, logLevel, format, a...)
	} else {
		goLog.Output(callDepth+1, fmt.Sprintf(format, a...))
	}
}

// Infof logs the string if the logger is at least InfoLevel.
func (l *Logger) Infof(format string, a ...interface{}) {
	l.infof(startDepth, format, a...)
}

// infof logs the string if the logger is at least InfoLevel.
func (l *Logger) infof(callDepth int, format string, a ...interface{}) {
	if l.LoggerLevel >= InfoLevel {
		l.log(callDepth+1, "", format, a...)
	}
}

func Infof(ctx context.Context, format string, a ...interface{}) {
	logf(startDepth, ctx, InfoLevel, format, a...)
}

// Debugf logs the string if the logger is at least DebugLevel.
func (l *Logger) Debugf(format string, a ...interface{}) {
	l.debugf(startDepth, format, a...)
}

// Debugf logs the string if the logger is at least DebugLevel.
func (l *Logger) debugf(callDepth int, format string, a ...interface{}) {
	if l.LoggerLevel >= DebugLevel {
		l.log(callDepth+1, l.color.Cyan("DEBUG: "), format, a...)
	}
}

func Debugf(ctx context.Context, format string, a ...interface{}) {
	logf(startDepth, ctx, DebugLevel, format, a...)
}

// Tracef logs the string if the logger is at least TraceLevel.
func (l *Logger) Tracef(format string, a ...interface{}) {
	l.tracef(startDepth, format, a...)
}

// Tracef logs the string if the logger is at least TraceLevel.
func (l *Logger) tracef(callDepth int, format string, a ...interface{}) {
	if l.LoggerLevel >= TraceLevel {
		l.log(callDepth+1, l.color.Blue("TRACE: "), format, a...)
	}
}

func Tracef(ctx context.Context, format string, a ...interface{}) {
	logf(startDepth, ctx, TraceLevel, format, a...)
}

// Warningf logs the string if the logger is at least WarningLevel.
func (l *Logger) Warningf(format string, a ...interface{}) {
	l.warningf(startDepth, format, a...)
}

// Warningf logs the string if the logger is at least WarningLevel.
func (l *Logger) warningf(callDepth int, format string, a ...interface{}) {
	if l.LoggerLevel >= WarningLevel {
		l.log(callDepth+1, l.color.Yellow("WARN: "), format, a...)
	}
}

func Warningf(ctx context.Context, format string, a ...interface{}) {
	logf(startDepth, ctx, WarningLevel, format, a...)
}

// Errorf logs the string if the logger is at least ErrorLevel.
func (l *Logger) Errorf(format string, a ...interface{}) {
	l.errorf(startDepth, format, a...)
}

// Errorf logs the string if the logger is at least ErrorLevel.
func (l *Logger) errorf(callDepth int, format string, a ...interface{}) {
	if l.LoggerLevel >= ErrorLevel {
		l.goErrorLogger.Output(callDepth+1, fmt.Sprintf("%s%s%s", l.prefix, l.color.Red("ERROR: "), fmt.Sprintf(format, a...)))
	}
}

func Errorf(ctx context.Context, format string, a ...interface{}) {
	logf(startDepth, ctx, ErrorLevel, format, a...)
}

// Fatalf logs the string if the logger is at least FatalLevel.
func (l *Logger) Fatalf(format string, a ...interface{}) {
	l.fatalf(startDepth, format, a...)
}

// Fatalf logs the string if the logger is at least FatalLevel.
func (l *Logger) fatalf(callDepth int, format string, a ...interface{}) {
	if l.LoggerLevel >= FatalLevel {
		l.goErrorLogger.Output(callDepth+1, fmt.Sprintf("%s%s%s", l.prefix, l.color.Red("FATAL: "), fmt.Sprintf(format, a...)))
		os.Exit(1)
	}
}

func Fatalf(ctx context.Context, format string, a ...interface{}) {
	logf(startDepth, ctx, FatalLevel, format, a...)
}
