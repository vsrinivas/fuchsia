// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package logger

import (
	"bytes"
	"context"
	"fmt"
	"os"
	"regexp"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/lib/color"
)

func TestWithContext(t *testing.T) {
	logger := NewLogger(DebugLevel, color.NewColor(color.ColorAuto), os.Stdout, os.Stderr, "")
	ctx := context.Background()
	if v, ok := ctx.Value(globalLoggerKeyType{}).(*Logger); ok || v != nil {
		t.Fatalf("Default context should not have globalLoggerKeyType. Expected: \nnil\n but got: \n%+v ", v)
	}
	ctx = WithLogger(ctx, logger)
	if v, ok := ctx.Value(globalLoggerKeyType{}).(*Logger); !ok || v == nil {
		t.Fatalf("Updated context should have globalLoggerKeyType, but got nil")
	}

}

func TestNewLogger(t *testing.T) {
	prefix := "testprefix "

	logger := NewLogger(InfoLevel, color.NewColor(color.ColorAuto), nil, nil, prefix)
	logFlags, errFlags := logger.goLogger.Flags(), logger.goErrorLogger.Flags()

	correctFlags := (Ldate | Lmicroseconds)

	if logFlags != correctFlags || errFlags != correctFlags {
		t.Fatalf("New loggers should have the proper flags set for both standard and error logging. Expected: \n%+v and %+v\n but got: \n%+v and %+v", correctFlags, correctFlags, logFlags, errFlags)
	}

	logPrefix := logger.prefix
	if logPrefix != prefix {
		t.Fatalf("New loggers should use the specified prefix on creation. Expected: \n%+v\n but got: \n%+v", prefix, logPrefix)
	}
}

func TestNewLoggerSetFlags(t *testing.T) {
	flags := Ldate | Lshortfile
	logger := NewLogger(InfoLevel, color.NewColor(color.ColorAuto), nil, nil, "")
	logger.SetFlags(flags)
	logFlags, errFlags := logger.goLogger.Flags(), logger.goErrorLogger.Flags()

	if logFlags != flags || errFlags != flags {
		t.Fatalf("Loggers should have the flags passed into `SetFlags`. Expected: \n%+v and %+v\n but got: \n%+v and %+v", flags, flags, logFlags, errFlags)
	}
}

func TestLogLevel(t *testing.T) {
	level := InfoLevel
	if level.String() != "info" {
		t.Errorf("InfoLevel.String() should return %q, got %q", "info", level.String())
	}

	level.Set("debug")
	if level != DebugLevel {
		t.Errorf("LogLevel.Set() should change the level's value, but value is still %q", level.String())
	}
}

func TestCallDepth(t *testing.T) {
	prefix := "cdprefix "
	infoLog, errLog := "Info log", "Error log"
	outBuffer, errBuffer := new(bytes.Buffer), new(bytes.Buffer)
	logger := NewLogger(DebugLevel, color.NewColor(color.ColorAuto), outBuffer, errBuffer, prefix)
	logger.SetFlags(Ldate | Lshortfile)

	logger.Infof(infoLog)
	logger.Errorf(errLog)

	outBytes, errBytes := outBuffer.Bytes(), errBuffer.Bytes()

	matched, err := regexp.Match(
		fmt.Sprintf(`\d{4}\/\d{2}\/\d{2} logger_test.go:\d+: %s%s`, prefix, infoLog),
		outBytes)
	if err != nil || !matched {
		t.Fatalf("Stdout output was not as expected. Got: %s", outBytes)
	}
	matched, err = regexp.Match(
		fmt.Sprintf(`\d{4}\/\d{2}\/\d{2} logger_test.go:\d+: %s%s%s`, prefix, regexp.QuoteMeta(logger.color.Red("ERROR: ")), errLog),
		errBytes)
	if err != nil || !matched {
		t.Fatalf("Stderr output was not as expected. Got: %s", errBytes)
	}
}

type counterPrefixer struct {
	counter int
}

func (p *counterPrefixer) String() string {
	p.counter++
	return fmt.Sprintf("%d ", p.counter)
}

func TestCustomPrefix(t *testing.T) {
	infoLog, errLog := "Info log", "Error log"
	outBuffer, errBuffer := new(bytes.Buffer), new(bytes.Buffer)

	logger := NewLogger(DebugLevel, color.NewColor(color.ColorAuto), outBuffer, errBuffer, &counterPrefixer{})
	logger.SetFlags(Ldate | Lshortfile)

	logger.Infof(infoLog)
	logger.Errorf(errLog)

	outBytes, errBytes := outBuffer.Bytes(), errBuffer.Bytes()

	matched, err := regexp.Match(
		fmt.Sprintf(`\d{4}\/\d{2}\/\d{2} logger_test.go:\d+: 1 %s`, infoLog),
		outBytes)
	if err != nil || !matched {
		t.Fatalf("Stdout output was not as expected. Got: %s", outBytes)
	}
	matched, err = regexp.Match(
		fmt.Sprintf(`\d{4}\/\d{2}\/\d{2} logger_test.go:\d+: 2 %s%s`, regexp.QuoteMeta(logger.color.Red("ERROR: ")), errLog),
		errBytes)
	if err != nil || !matched {
		t.Fatalf("Stderr output was not as expected. Got: %s", errBytes)
	}
}
