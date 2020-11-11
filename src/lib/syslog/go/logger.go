// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !build_with_native_toolchain

package syslog

import (
	"context"
	"encoding/binary"
	"flag"
	"fmt"
	"io"
	"os"
	"runtime"
	"strconv"
	"strings"
	"sync/atomic"
	"syscall/zx"
	"unicode/utf8"

	"go.fuchsia.dev/fuchsia/src/lib/component"

	"fidl/fuchsia/logger"

	"github.com/pkg/errors"
)

// ErrMsgTooLong is returned when a message is truncated.
type ErrMsgTooLong struct {
	// Msg is the truncated part of the message.
	Msg string
}

func (e *ErrMsgTooLong) Error() string {
	return "too long message was truncated"
}

const (
	_ = iota
	DebugVerbosity
	TraceVerbosity
)

type LogLevel int32

const (
	AllLevel     = LogLevel(logger.LogLevelFilterAll)
	TraceLevel   = LogLevel(logger.LogLevelFilterTrace)
	DebugLevel   = LogLevel(logger.LogLevelFilterDebug)
	InfoLevel    = LogLevel(logger.LogLevelFilterInfo)
	WarningLevel = LogLevel(logger.LogLevelFilterWarn)
	ErrorLevel   = LogLevel(logger.LogLevelFilterError)
	FatalLevel   = LogLevel(logger.LogLevelFilterFatal)
	NoneLevel    = LogLevel(logger.LogLevelFilterNone)
)

var _ flag.Value = (*LogLevel)(nil)

// Set implements the flag.Value interface.
func (ll *LogLevel) Set(s string) error {
	s = strings.ToUpper(s)
	lls := logLevelFromString(s)
	if lls != NoneLevel {
		*ll = lls
		return nil
	}
	v, err := strconv.Atoi(s)
	if err != nil {
		return err
	}
	*ll = LogLevel(v)
	return nil
}

func logLevelFromString(s string) LogLevel {
	switch s {
	case "ALL":
		return AllLevel
	case "TRACE":
		return TraceLevel
	case "DEBUG":
		return DebugLevel
	case "INFO":
		return InfoLevel
	case "WARNING":
		return WarningLevel
	case "ERROR":
		return ErrorLevel
	case "FATAL":
		return FatalLevel
	default:
		return NoneLevel
	}
}

func (ll LogLevel) String() string {
	switch ll {
	case AllLevel:
		return "ALL"
	case TraceLevel:
		return "TRACE"
	case DebugLevel:
		return "DEBUG"
	case InfoLevel:
		return "INFO"
	case WarningLevel:
		return "WARNING"
	case ErrorLevel:
		return "ERROR"
	case FatalLevel:
		return "FATAL"
	default:
		return fmt.Sprintf("LOG(%d)", ll)
	}
}

type Writer struct {
	*Logger
}

func (l *Writer) Write(data []byte) (n int, err error) {
	origLen := len(data)

	// Strip out the trailing newline the `log` library adds because the
	// logging service also adds a trailing newline.
	if len(data) > 0 && data[len(data)-1] == '\n' {
		data = data[:len(data)-1]
	}

	if err := l.Infof("%s", data); err != nil {
		return 0, err
	}

	return origLen, nil
}

func ConnectToLogger(c *component.Connector) (zx.Socket, error) {
	localS, peerS, err := zx.NewSocket(zx.SocketDatagram)
	if err != nil {
		return zx.Socket(zx.HandleInvalid), err
	}
	req, logSink, err := logger.NewLogSinkWithCtxInterfaceRequest()
	if err != nil {
		_ = localS.Close()
		_ = peerS.Close()
		return zx.Socket(zx.HandleInvalid), err
	}
	c.ConnectToEnvService(req)
	{
		err := logSink.Connect(context.Background(), peerS)
		_ = logSink.Close()
		if err != nil {
			_ = localS.Close()
			return zx.Socket(zx.HandleInvalid), err
		}
	}
	return localS, nil
}

type LogInitOptions struct {
	LogLevel                      LogLevel // Accessed atomically.
	MinSeverityForFileAndLineInfo LogLevel
	Socket                        zx.Socket
	Tags                          []string
	Writer                        io.Writer
}

type Logger struct {
	droppedLogs uint32
	options     LogInitOptions
	pid         uint64
}

func NewLogger(options LogInitOptions) (*Logger, error) {
	if l, max := len(options.Tags), int(logger.MaxTags); l > max {
		return nil, errors.Errorf("too many tags: %d/%d", l, max)
	}
	for _, tag := range options.Tags {
		if l, max := len(tag), int(logger.MaxTagLenBytes); l > max {
			return nil, errors.Errorf("tag too long: %d/%d", l, max)
		}
	}
	return &Logger{
		options: options,
		pid:     uint64(os.Getpid()),
	}, nil
}

func NewLoggerWithDefaults(c *component.Connector, tags ...string) (*Logger, error) {
	s, err := ConnectToLogger(c)
	if err != nil {
		return nil, err
	}
	return NewLogger(LogInitOptions{
		LogLevel:                      InfoLevel,
		MinSeverityForFileAndLineInfo: ErrorLevel,
		Socket:                        s,
		Tags:                          tags,
	})
}

func (l *Logger) logToWriter(writer io.Writer, time zx.Time, logLevel LogLevel, tag, msg string) error {
	if writer == nil {
		writer = os.Stderr
	}
	var tagsStorage [logger.MaxTags + 1]string
	tags := tagsStorage[:0]
	for _, tag := range l.options.Tags {
		if len(tag) > 0 {
			tags = append(tags, tag)
		}
	}
	if len(tag) > 0 {
		tags = append(tags, tag)
	}
	var buffer [int(logger.MaxTags+1) * int(logger.MaxTagLenBytes)]byte
	pos := 0
	for i, tag := range tags {
		if i > 0 {
			pos += copy(buffer[pos:], ", ")
		}
		pos += copy(buffer[pos:], tag)
	}
	_, err := fmt.Fprintf(writer, "[%05d.%06d][%d][0][%s] %s: %s\n", time/1000000000, (time/1000)%1000000, l.pid, buffer[:pos], logLevel, msg)
	return err
}

func (l *Logger) logToSocket(time zx.Time, logLevel LogLevel, tag, msg string) error {
	const golangThreadID = 0

	var buffer [logger.MaxDatagramLenBytes]byte

	pos := 0
	for _, i := range [...]uint64{
		l.pid,
		golangThreadID,
		uint64(time),
	} {
		binary.LittleEndian.PutUint64(buffer[pos:], i)
		pos += 8
	}
	for _, i := range [...]uint32{
		uint32(logLevel),
		atomic.LoadUint32(&l.droppedLogs),
	} {
		binary.LittleEndian.PutUint32(buffer[pos:], i)
		pos += 4
	}

	// Write global tags
	for _, tag := range l.options.Tags {
		if length := len(tag); length != 0 {
			buffer[pos] = byte(length)
			pos += 1
			pos += copy(buffer[pos:], tag)
		}
	}

	// Write local tags
	if length := len(tag); length != 0 {
		buffer[pos] = byte(length)
		pos += 1
		pos += copy(buffer[pos:], tag)
	}

	const ellipsis = "..."

	// Write msg
	buffer[pos] = 0
	pos += 1

	payload := msg
	if len(payload)+1 > len(buffer)-pos {
		payload = payload[:len(buffer)-pos-1-len(ellipsis)]

		// Remove the last byte until the result is valid UTF-8.
		for {
			if rune, _ := utf8.DecodeLastRuneInString(payload); rune != utf8.RuneError {
				break
			}
			payload = payload[:len(payload)-1]
		}
	}
	pos += copy(buffer[pos:], payload)
	if payload != msg {
		pos += copy(buffer[pos:], ellipsis)
	}

	buffer[pos] = 0
	pos += 1

	if _, err := l.options.Socket.Write(buffer[:pos], 0); err != nil {
		atomic.AddUint32(&l.droppedLogs, 1)
		return err
	}

	if payload != msg {
		return &ErrMsgTooLong{Msg: msg[len(payload):]}
	}
	return nil
}

func (l *Logger) logf(callDepth int, logLevel LogLevel, tag string, format string, a ...interface{}) error {
	if LogLevel(atomic.LoadInt32((*int32)(&l.options.LogLevel))) > logLevel {
		return nil
	}
	time := zx.Sys_clock_get_monotonic()
	msg := fmt.Sprintf(format, a...)
	if logLevel >= l.options.MinSeverityForFileAndLineInfo {
		_, file, line, ok := runtime.Caller(callDepth)
		if !ok {
			file = "???"
			line = 0
		} else {
			short := file
			for i := len(file) - 1; i > 0; i-- {
				if file[i] == '/' {
					short = file[i+1:]
					break
				}
			}
			file = short
		}
		msg = fmt.Sprintf("%s(%d): %s ", file, line, msg)
	}
	if len(tag) > int(logger.MaxTagLenBytes) {
		tag = tag[:logger.MaxTagLenBytes]
	}
	if logLevel == FatalLevel {
		defer os.Exit(1)
	}
	if atomic.LoadUint32((*uint32)(&l.options.Socket)) != uint32(zx.HandleInvalid) {
		switch err := l.logToSocket(time, logLevel, tag, msg).(type) {
		case *zx.Error:
			switch err.Status {
			case zx.ErrPeerClosed, zx.ErrBadState:
				atomic.StoreUint32((*uint32)(&l.options.Socket), uint32(zx.HandleInvalid))
			default:
				return err
			}
		default:
			return err
		}
	}
	return l.logToWriter(l.options.Writer, time, logLevel, tag, msg)
}

func (l *Logger) SetSeverity(logLevel LogLevel) {
	atomic.StoreInt32((*int32)(&l.options.LogLevel), int32(logLevel))
}

// severityFromVerbosity provides the severity corresponding to the given
// verbosity. Note that verbosity relative to the default severity and can
// be thought of as incrementally "more vebose than" the baseline.
func severityFromVerbosity(verbosity int) LogLevel {
	if verbosity < 0 {
		verbosity = 0
	}

	level := InfoLevel - LogLevel(uint8(verbosity)*logger.LogVerbosityStepSize)
	// verbosity scale sits in the interstitial space between INFO and DEBUG
	if level < (DebugLevel + 1) {
		return DebugLevel + 1
	}
	return level
}

func (l *Logger) SetVerbosity(verbosity int) {
	atomic.StoreInt32((*int32)(&l.options.LogLevel), int32(severityFromVerbosity(verbosity)))
}

func (l *Logger) Tracef(format string, a ...interface{}) error {
	return l.logf(2, TraceLevel, "", format, a...)
}

func (l *Logger) Debugf(format string, a ...interface{}) error {
	return l.logf(2, DebugLevel, "", format, a...)
}

func (l *Logger) Infof(format string, a ...interface{}) error {
	return l.logf(2, InfoLevel, "", format, a...)
}

func (l *Logger) Warnf(format string, a ...interface{}) error {
	return l.logf(2, WarningLevel, "", format, a...)
}

func (l *Logger) Errorf(format string, a ...interface{}) error {
	return l.logf(2, ErrorLevel, "", format, a...)
}

func (l *Logger) Fatalf(format string, a ...interface{}) error {
	return l.logf(2, FatalLevel, "", format, a...)
}

func (l *Logger) VLogf(verbosity int, format string, a ...interface{}) error {
	return l.logf(2, severityFromVerbosity(verbosity), "", format, a...)
}

func (l *Logger) TraceTf(tag, format string, a ...interface{}) error {
	return l.logf(2, TraceLevel, tag, format, a...)
}

func (l *Logger) DebugTf(tag, format string, a ...interface{}) error {
	return l.logf(2, DebugLevel, tag, format, a...)
}

func (l *Logger) InfoTf(tag, format string, a ...interface{}) error {
	return l.logf(2, InfoLevel, tag, format, a...)
}

func (l *Logger) WarnTf(tag, format string, a ...interface{}) error {
	return l.logf(2, WarningLevel, tag, format, a...)
}

func (l *Logger) ErrorTf(tag, format string, a ...interface{}) error {
	return l.logf(2, ErrorLevel, tag, format, a...)
}

func (l *Logger) FatalTf(tag, format string, a ...interface{}) error {
	return l.logf(2, FatalLevel, tag, format, a...)
}

func (l *Logger) VLogTf(verbosity int, tag, format string, a ...interface{}) error {
	return l.logf(2, severityFromVerbosity(verbosity), tag, format, a...)
}

var defaultLogger = &Logger{
	options: LogInitOptions{
		LogLevel:                      InfoLevel,
		MinSeverityForFileAndLineInfo: ErrorLevel,
		Writer:                        os.Stderr,
	},
	pid: uint64(os.Getpid()),
}

func logf(callDepth int, logLevel LogLevel, tag string, format string, a ...interface{}) error {
	if l := GetDefaultLogger(); l != nil {
		return l.logf(callDepth+1, logLevel, tag, format, a...)
	}
	return errors.New("default logger not initialized")
}

// Public APIs for default logger

func GetDefaultLogger() *Logger {
	return defaultLogger
}

func SetDefaultLogger(l *Logger) {
	defaultLogger = l
}

func SetSeverity(logLevel LogLevel) {
	if l := GetDefaultLogger(); l != nil {
		atomic.StoreInt32((*int32)(&l.options.LogLevel), int32(logLevel))
	}
}

func SetVerbosity(verbosity int) {
	if l := GetDefaultLogger(); l != nil {
		atomic.StoreInt32((*int32)(&l.options.LogLevel), int32(severityFromVerbosity(verbosity)))
	}
}

func Tracef(format string, a ...interface{}) error {
	return logf(2, TraceLevel, "", format, a...)
}

func Debugf(format string, a ...interface{}) error {
	return logf(2, DebugLevel, "", format, a...)
}

func Infof(format string, a ...interface{}) error {
	return logf(2, InfoLevel, "", format, a...)
}

func Warnf(format string, a ...interface{}) error {
	return logf(2, WarningLevel, "", format, a...)
}

func Errorf(format string, a ...interface{}) error {
	return logf(2, ErrorLevel, "", format, a...)
}

func Fatalf(format string, a ...interface{}) error {
	return logf(2, FatalLevel, "", format, a...)
}

func VLogf(verbosity int, format string, a ...interface{}) error {
	return logf(2, severityFromVerbosity(verbosity), "", format, a...)
}

func TraceTf(tag, format string, a ...interface{}) error {
	return logf(2, TraceLevel, tag, format, a...)
}

func DebugTf(tag, format string, a ...interface{}) error {
	return logf(2, DebugLevel, tag, format, a...)
}

func InfoTf(tag, format string, a ...interface{}) error {
	return logf(2, InfoLevel, tag, format, a...)
}

func WarnTf(tag, format string, a ...interface{}) error {
	return logf(2, WarningLevel, tag, format, a...)
}

func ErrorTf(tag, format string, a ...interface{}) error {
	return logf(2, ErrorLevel, tag, format, a...)
}

func FatalTf(tag, format string, a ...interface{}) error {
	return logf(2, FatalLevel, tag, format, a...)
}

func VLogTf(verbosity int, tag, format string, a ...interface{}) error {
	return logf(2, severityFromVerbosity(verbosity), tag, format, a...)
}
