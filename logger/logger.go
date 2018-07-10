// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package logger

import (
	"context"
	"fmt"
	"io"
	goLog "log"
	"os"

	"fuchsia.googlesource.com/tools/color"
)

type globalLoggerKeyType struct{}

func WithLogger(ctx context.Context, logger *Logger) context.Context {
	return context.WithValue(ctx, globalLoggerKeyType{}, logger)
}

type Logger struct {
	LoggerLevel   LogLevel
	goLogger      *goLog.Logger
	goErrorLogger *goLog.Logger
	color         color.Color
}

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

func NewLogger(loggerLevel LogLevel, color color.Color, outWriter, errWriter io.Writer) *Logger {
	if outWriter == nil {
		outWriter = os.Stdout
	}
	if errWriter == nil {
		errWriter = os.Stderr
	}
	l := &Logger{
		LoggerLevel:   loggerLevel,
		goLogger:      goLog.New(outWriter, "", 0),
		goErrorLogger: goLog.New(errWriter, "", 0),
		color:         color,
	}
	return l
}

func (l *Logger) log(prefix, format string, a ...interface{}) {
	l.goLogger.Printf("%s%s", prefix, fmt.Sprintf(format, a...))
}

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

func (l *Logger) Infof(format string, a ...interface{}) {
	if l.LoggerLevel >= InfoLevel {
		l.log("", format, a...)
	}
}

func Infof(ctx context.Context, format string, a ...interface{}) {
	Logf(ctx, InfoLevel, format, a...)
}

func (l *Logger) Debugf(format string, a ...interface{}) {
	if l.LoggerLevel >= DebugLevel {
		l.log(l.color.Cyan("DEBUG: "), format, a...)
	}
}

func Debugf(ctx context.Context, format string, a ...interface{}) {
	Logf(ctx, DebugLevel, format, a...)
}

func (l *Logger) Tracef(format string, a ...interface{}) {
	if l.LoggerLevel >= TraceLevel {
		l.log(l.color.Blue("TRACE: "), format, a...)
	}
}

func Tracef(ctx context.Context, format string, a ...interface{}) {
	Logf(ctx, TraceLevel, format, a...)
}

func (l *Logger) Warningf(format string, a ...interface{}) {
	if l.LoggerLevel >= WarningLevel {
		l.log(l.color.Yellow("WARN: "), format, a...)
	}
}

func Warningf(ctx context.Context, format string, a ...interface{}) {
	Logf(ctx, WarningLevel, format, a...)
}

func (l *Logger) Errorf(format string, a ...interface{}) {
	if l.LoggerLevel >= ErrorLevel {
		l.goErrorLogger.Printf("%s%s", l.color.Red("ERROR: "), fmt.Sprintf(format, a...))
	}
}

func Errorf(ctx context.Context, format string, a ...interface{}) {
	Logf(ctx, ErrorLevel, format, a...)
}

func (l *Logger) Fatalf(format string, a ...interface{}) {
	if l.LoggerLevel >= FatalLevel {
		l.goErrorLogger.Fatalf("%s%s", l.color.Red("FATAL: "), fmt.Sprintf(format, a...))
	}
}

func Fatalf(ctx context.Context, format string, a ...interface{}) {
	Logf(ctx, FatalLevel, format, a...)
}
