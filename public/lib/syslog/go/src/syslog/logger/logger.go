// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package logger

import (
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"os"
	"runtime"
	"strings"
	"sync"
	"sync/atomic"
	"syscall/zx"

	"app/context"

	logger_fidl "fidl/logger"
)

var (
	ErrNotInitialized   = errors.New("default logger not initialized")
	ErrInitialized      = errors.New("default logger is already initialized")
	ErrInvalidArg       = errors.New("Invalid arguments to logger")
	ErrMsgTooLongString = "Msg too long. it was truncated"
)

// This is returned when msg is truncated. It contains the truncated part.
type ErrMsgTooLong struct {
	Msg string // rest of the message
}

func (e ErrMsgTooLong) Error() string {
	return ErrMsgTooLongString
}

const (
	MAX_GLOBAL_TAGS   = 4
	MAX_TAG_LEN       = 63
	SOCKET_BUFFER_LEN = 2032
	SOCKET_DATAGRAM   = 1 << 0
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

type LogInitOptions struct {
	Connector         *context.Connector
	Loglevel          LogLevel
	ConsoleWriter     io.Writer
	LogServiceChannel *zx.Socket
	Tags              []string
}

func GetDefaultInitOptions() LogInitOptions {
	return LogInitOptions{
		Loglevel:          InfoLevel,
		ConsoleWriter:     nil,
		LogServiceChannel: nil,
		Tags:              nil,
	}
}

type Logger struct {
	logLevel    LogLevel
	tags        []string
	socket      *zx.Socket
	writer      atomic.Value
	tagString   string
	pid         uint64
	droppedLogs uint32
	fallbackMux sync.Mutex
}

func (l *Logger) setTags(tags []string) error {
	if len(tags) > MAX_GLOBAL_TAGS {
		return ErrInvalidArg
	}
	for _, tag := range tags {
		if len(tag) > MAX_TAG_LEN {
			return ErrInvalidArg
		}
	}
	if l.writer.Load() != nil {
		l.tagString = strings.Join(tags, ", ")
	}
	l.tags = tags
	return nil
}

func (l *Logger) ActivateFallbackMode() {
	l.fallbackMux.Lock()
	defer l.fallbackMux.Unlock()
	if l.writer.Load() != nil {
		return //already active
	}
	if l.tagString == "" {
		l.tagString = strings.Join(l.tags, ", ")
	}
	l.writer.Store(os.Stderr)
}

func connectToLogger(c *context.Connector) (*zx.Socket, error) {
	sin, sout, err := zx.NewSocket(SOCKET_DATAGRAM)
	if err != nil {
		return nil, err
	}
	req, ls, err := logger_fidl.NewLogSinkInterfaceRequest()
	if err != nil {
		return nil, err
	}
	c.ConnectToEnvService(req)
	err = ls.Connect(sout)
	ls.Close()
	return &sin, err
}

func NewLogger(options LogInitOptions) (*Logger, error) {
	l := Logger{
		logLevel: options.Loglevel,
		socket:   options.LogServiceChannel,
		pid:      uint64(os.Getpid()),
	}
	if options.ConsoleWriter == nil && options.LogServiceChannel == nil {
		if options.Connector == nil {
			return nil, fmt.Errorf("Init Error: Writer, LogServiceChannel or Connector needs to be provided")
		}
		if sock, err := connectToLogger(options.Connector); err != nil {
			fmt.Fprintf(os.Stderr, "not able to conenct to log sink, will write logs to stderr: %s", err)
			l.writer.Store(os.Stderr)
		} else {
			l.socket = sock
		}
	} else if options.ConsoleWriter != nil {
		l.writer.Store(options.ConsoleWriter)
	}
	if err := l.setTags(options.Tags); err != nil {
		return nil, err
	}
	return &l, nil
}

func (l *Logger) logToWriter(writer io.Writer, time zx.Time, logLevel LogLevel, tag, msg string) error {
	if len(l.tagString) != 0 {
		if len(tag) != 0 {
			tag = fmt.Sprintf("%s, %s", l.tagString, tag)
		} else {
			tag = l.tagString
		}
	}
	_, err := io.WriteString(writer, fmt.Sprintf("[%05d.%06d][%d][0][%s] %s: %s\n", time/1000000000, (time/1000)%1000000, l.pid, tag, logLevel, msg))
	return err
}

func (l *Logger) logToSocket(time zx.Time, logLevel LogLevel, tag, msg string) error {
	var buffer [SOCKET_BUFFER_LEN]byte
	binary.LittleEndian.PutUint64(buffer[0:8], l.pid)
	binary.LittleEndian.PutUint64(buffer[8:16], 0) // golang doesn't have tid
	binary.LittleEndian.PutUint64(buffer[16:24], uint64(time))
	binary.LittleEndian.PutUint32(buffer[24:28], uint32(logLevel))
	binary.LittleEndian.PutUint32(buffer[28:32], atomic.LoadUint32(&l.droppedLogs))
	pos := 32

	// Write global tags
	for _, tag := range l.tags {
		length := len(tag)
		if length == 0 {
			continue
		}
		buffer[pos] = byte(length)
		pos = pos + 1
		copy(buffer[pos:pos+length], tag)
		pos = pos + length
	}

	// Write local tags
	if len(tag) != 0 {
		length := len(tag)
		buffer[pos] = byte(length)
		pos = pos + 1
		copy(buffer[pos:pos+length], tag)
		pos = pos + length
	}

	//write msg
	buffer[pos] = 0
	pos = pos + 1
	errMsgTooLong := ErrMsgTooLong{""}
	if len(msg) >= SOCKET_BUFFER_LEN-pos {
		// truncate
		errMsgTooLong.Msg = msg[SOCKET_BUFFER_LEN-pos-4:]
		msg = msg[:SOCKET_BUFFER_LEN-pos-4]
		copy(buffer[pos:], msg)
		copy(buffer[pos+len(msg):], "...")
		pos = pos + len(msg) + 3
	} else {
		copy(buffer[pos:], msg)
		pos = pos + len(msg)
	}

	buffer[pos] = 0
	if _, err := l.socket.Write(buffer[:pos+1], 0); err != nil {
		atomic.AddUint32(&l.droppedLogs, 1)
		return err
	}
	if errMsgTooLong.Msg != "" {
		return errMsgTooLong
	}
	return nil
}

func (l *Logger) logf(callDepth int, logLevel LogLevel, tag string, format string, a ...interface{}) error {
	if l.logLevel > logLevel {
		return nil
	}
	time := zx.Sys_clock_get(0) //MONOTONIC
	msg := fmt.Sprintf(format, a...)
	if logLevel >= ErrorLevel {
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
	if len(tag) > MAX_TAG_LEN {
		tag = tag[:MAX_TAG_LEN]
	}
	if logLevel == FatalLevel {
		defer os.Exit(1)
	}
	w := l.writer.Load()
	if w != nil {
		return l.logToWriter(w.(io.Writer), time, logLevel, tag, msg)
	} else {
		if err := l.logToSocket(time, logLevel, tag, msg); err != nil {
			if status, ok := err.(zx.Error); ok {
				if status.Status == zx.ErrPeerClosed || status.Status == zx.ErrBadState {
					l.ActivateFallbackMode()
					w = l.writer.Load()
					if w != nil {
						return l.logToWriter(w.(io.Writer), time, logLevel, tag, msg)
					}
				}
			}
			return err
		}
	}
	return nil
}

func (l *Logger) SetSeverity(logLevel LogLevel) {
	l.logLevel = logLevel
}

func (l *Logger) SetVerbosity(verbosity int) {
	l.logLevel = LogLevel(-verbosity)
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
	return l.logf(2, LogLevel(-verbosity), "", format, a...)
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
	return l.logf(2, LogLevel(-verbosity), tag, format, a...)
}

var defaultLogger *Logger

func logf(callDepth int, logLevel LogLevel, tag string, format string, a ...interface{}) error {
	l := GetDefaultLogger()
	if l != nil {
		return l.logf(callDepth+1, logLevel, tag, format, a...)
	} else {
		return ErrNotInitialized
	}
}

// Public APIs for default logger

func GetDefaultLogger() *Logger {
	return defaultLogger
}

func InitDefaultLogger(c *context.Connector) error {
	options := GetDefaultInitOptions()
	options.Connector = c
	return InitDefaultLoggerWithConfig(options)
}

func InitDefaultLoggerWithTags(c *context.Connector, tags ...string) error {
	o := GetDefaultInitOptions()
	o.Tags = tags
	o.Connector = c
	return InitDefaultLoggerWithConfig(o)
}

func InitDefaultLoggerWithConfig(options LogInitOptions) error {
	if defaultLogger != nil {
		return ErrInitialized
	}

	if l, err := NewLogger(options); err != nil {
		return err
	} else {
		defaultLogger = l
	}
	return nil
}

func SetSeverity(logLevel LogLevel) {
	if l := GetDefaultLogger(); l != nil {
		l.logLevel = logLevel
	}
}

func SetVerbosity(verbosity int) {
	if l := GetDefaultLogger(); l != nil {
		l.logLevel = LogLevel(-verbosity)
	}
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
	return logf(2, LogLevel(-verbosity), "", format, a...)
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
	return logf(2, LogLevel(-verbosity), tag, format, a...)
}

func ToErrMsgTooLong(e error) *ErrMsgTooLong {
	if err, ok := e.(ErrMsgTooLong); ok {
		return &err
	} else {
		return nil
	}
}
