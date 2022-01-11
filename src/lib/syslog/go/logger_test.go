// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain
// +build !build_with_native_toolchain

package syslog_test

import (
	"bytes"
	"context"
	"encoding/binary"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"
	"syscall/zx"
	"syscall/zx/fidl"
	"syscall/zx/zxwait"
	"testing"
	"unicode/utf8"

	"fidl/fuchsia/diagnostics"
	"fidl/fuchsia/logger"
	"go.fuchsia.dev/fuchsia/src/lib/component"
	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"
)

const format = "integer: %d"

var pid = uint64(os.Getpid())

var _ logger.LogSinkWithCtx = (*logSinkImpl)(nil)

type logSinkImpl struct {
	onConnect func(fidl.Context, zx.Socket) error
}

func (impl *logSinkImpl) Connect(ctx fidl.Context, socket zx.Socket) error {
	return impl.onConnect(ctx, socket)
}

func (*logSinkImpl) ConnectStructured(fidl.Context, zx.Socket) error {
	return nil
}

func (*logSinkImpl) WaitForInterestChange(fidl.Context) (logger.LogSinkWaitForInterestChangeResult, error) {
	return logger.LogSinkWaitForInterestChangeResult{}, nil
}

func TestLogSimple(t *testing.T) {
	actual := bytes.Buffer{}
	log, err := syslog.NewLogger(syslog.LogInitOptions{
		MinSeverityForFileAndLineInfo: syslog.ErrorLevel,
		Writer:                        &actual,
	})
	if err != nil {
		t.Fatal(err)
	}
	if err := log.Infof(format, 10); err != nil {
		t.Fatal(err)
	}
	expected := "INFO: integer: 10\n"
	got := string(actual.Bytes())
	if !strings.HasSuffix(got, expected) {
		t.Errorf("%q should have ended in %q", got, expected)
	}
	if !strings.Contains(got, fmt.Sprintf("[%d]", pid)) {
		t.Errorf("%q should contains %d", got, pid)
	}
}

func setup(t *testing.T, tags ...string) (*logger.LogSinkEventProxy, zx.Socket, *syslog.Logger) {
	req, logSink, err := logger.NewLogSinkWithCtxInterfaceRequest()
	if err != nil {
		t.Fatal(err)
	}

	ctx, cancel := context.WithCancel(context.Background())
	ch := make(chan struct{})
	t.Cleanup(func() {
		cancel()
		<-ch
	})

	sinChan := make(chan zx.Socket, 1)
	defer close(sinChan)
	go func() {
		defer close(ch)

		component.Serve(ctx, &logger.LogSinkWithCtxStub{
			Impl: &logSinkImpl{
				onConnect: func(_ fidl.Context, socket zx.Socket) error {
					sinChan <- socket
					return nil
				},
			},
		}, req.Channel, component.ServeOptions{
			OnError: func(err error) {
				switch err := err.(type) {
				case *zx.Error:
					if err.Status == zx.ErrCanceled {
						return
					}
				}
				t.Error(err)
			},
		})
	}()

	log, err := syslog.NewLogger(syslog.LogInitOptions{
		LogSink:                       logSink,
		LogLevel:                      syslog.InfoLevel,
		MinSeverityForFileAndLineInfo: syslog.ErrorLevel,
		Tags:                          tags,
	})
	if err != nil {
		t.Fatal(err)
	}

	s := <-sinChan

	// Throw away system-generated messages.
	for i := 0; i < 1; i++ {
		if _, err := zxwait.WaitContext(context.Background(), zx.Handle(s), zx.SignalSocketReadable); err != nil {
			t.Fatal(err)
		}
		var data [logger.MaxDatagramLenBytes]byte
		if _, err := s.Read(data[:], 0); err != nil {
			t.Fatal(err)
		}
	}

	return &logger.LogSinkEventProxy{
		Channel: req.Channel,
	}, s, log
}

func checkoutput(t *testing.T, sin zx.Socket, expectedMsg string, severity syslog.LogLevel, tags ...string) {
	var data [logger.MaxDatagramLenBytes]byte
	n, err := sin.Read(data[:], 0)
	if err != nil {
		t.Fatal(err)
	}
	if n <= 32 {
		t.Fatalf("got invalid data: %x", data[:n])
	}
	gotpid := binary.LittleEndian.Uint64(data[0:8])
	gotTid := binary.LittleEndian.Uint64(data[8:16])
	gotTime := binary.LittleEndian.Uint64(data[16:24])
	gotSeverity := int32(binary.LittleEndian.Uint32(data[24:28]))
	gotDroppedLogs := int32(binary.LittleEndian.Uint32(data[28:32]))

	if pid != gotpid {
		t.Errorf("pid error, got: %d, want: %d", gotpid, pid)
	}

	if 0 != gotTid {
		t.Errorf("tid error, got: %d, want: %d", gotTid, 0)
	}

	if int32(severity) != gotSeverity {
		t.Errorf("severity error, got: %d, want: %d", gotSeverity, severity)
	}

	if gotTime <= 0 {
		t.Errorf("time %d should be greater than zero", gotTime)
	}

	if 0 != gotDroppedLogs {
		t.Errorf("dropped logs error, got: %d, want: %d", gotDroppedLogs, 0)
	}
	pos := 32
	for i, tag := range tags {
		length := len(tag)
		if data[pos] != byte(length) {
			t.Fatalf("tag iteration %d: expected data to be %d at pos %d, got %d", i, length, pos, data[pos])
		}
		pos = pos + 1
		gotTag := string(data[pos : pos+length])
		if tag != gotTag {
			t.Fatalf("tag iteration %d: expected tag %q , got %q", i, tag, gotTag)
		}
		pos = pos + length
	}

	if data[pos] != 0 {
		t.Fatalf("byte before msg start should be zero, got: %d, %x", data[pos], data[pos:n])
	}

	msgGot := string(data[pos+1 : n-1])
	if expectedMsg != msgGot {
		t.Fatalf("expected msg:%q, got %q", expectedMsg, msgGot)
	}
	if data[n-1] != 0 {
		t.Fatalf("last byte should be zero, got: %d, %x", data[pos], data[32:n])
	}
}

func TestLog(t *testing.T) {
	_, sin, log := setup(t)
	defer func() {
		if err := log.Close(); err != nil {
			t.Error(err)
		}
		if err := sin.Close(); err != nil {
			t.Error(err)
		}
	}()

	if err := log.Infof(format, 10); err != nil {
		t.Fatal(err)
	}
	checkoutput(t, sin, fmt.Sprintf(format, 10), syslog.InfoLevel)
}

func TestLogWithLocalTag(t *testing.T) {
	_, sin, log := setup(t)
	defer func() {
		if err := log.Close(); err != nil {
			t.Error(err)
		}
		if err := sin.Close(); err != nil {
			t.Error(err)
		}
	}()
	if err := log.InfoTf("local_tag", format, 10); err != nil {
		t.Fatal(err)
	}
	expectedMsg := fmt.Sprintf(format, 10)
	checkoutput(t, sin, expectedMsg, syslog.InfoLevel, "local_tag")
}

func TestLogWithGlobalTags(t *testing.T) {
	_, sin, log := setup(t, "gtag1", "gtag2")
	defer func() {
		if err := log.Close(); err != nil {
			t.Error(err)
		}
		if err := sin.Close(); err != nil {
			t.Error(err)
		}
	}()
	if err := log.InfoTf("local_tag", format, 10); err != nil {
		t.Fatal(err)
	}
	expectedMsg := fmt.Sprintf(format, 10)
	checkoutput(t, sin, expectedMsg, syslog.InfoLevel, "gtag1", "gtag2", "local_tag")
}

func TestLoggerSeverity(t *testing.T) {
	_, sin, log := setup(t)
	defer func() {
		if err := log.Close(); err != nil {
			t.Error(err)
		}
		if err := sin.Close(); err != nil {
			t.Error(err)
		}
	}()
	log.SetSeverity(diagnostics.Severity(syslog.WarningLevel))
	if err := log.Infof(format, 10); err != nil {
		t.Fatal(err)
	}
	_, err := sin.Read(nil, 0)
	if err, ok := err.(*zx.Error); !ok || err.Status != zx.ErrShouldWait {
		t.Fatal(err)
	}
	if err := log.Warnf(format, 10); err != nil {
		t.Fatal(err)
	}
	expectedMsg := fmt.Sprintf(format, 10)
	checkoutput(t, sin, expectedMsg, syslog.WarningLevel)
}

func TestLoggerVerbosity(t *testing.T) {
	_, sin, log := setup(t)
	defer func() {
		if err := log.Close(); err != nil {
			t.Error(err)
		}
		if err := sin.Close(); err != nil {
			t.Error(err)
		}
	}()
	if err := log.VLogf(syslog.DebugVerbosity, format, 10); err != nil {
		t.Fatal(err)
	}
	_, err := sin.Read(nil, 0)
	if err, ok := err.(*zx.Error); !ok || err.Status != zx.ErrShouldWait {
		t.Fatal(err)
	}
	log.SetVerbosity(syslog.DebugVerbosity)
	if err := log.VLogf(syslog.DebugVerbosity, format, 10); err != nil {
		t.Fatal(err)
	}
	expectedMsg := fmt.Sprintf(format, 10)
	checkoutput(t, sin, expectedMsg, syslog.InfoLevel-1)
}

func TestLoggerRegisterInterest(t *testing.T) {
	proxy, sin, log := setup(t)
	defer func() {
		if err := log.Close(); err != nil {
			t.Error(err)
		}
		if err := sin.Close(); err != nil {
			t.Error(err)
		}
	}()

	registerInterest := func(interest diagnostics.Interest) {
		t.Helper()

		if err := proxy.OnRegisterInterest(interest); err != nil {
			t.Fatal(err)
		}
		// Consume the system-generated messages.
		for i := 0; i < 2; i++ {
			if _, err := zxwait.WaitContext(context.Background(), zx.Handle(sin), zx.SignalSocketReadable); err != nil {
				t.Fatal(err)
			}
			var data [logger.MaxDatagramLenBytes]byte
			if _, err := sin.Read(data[:], 0); err != nil {
				t.Fatal(err)
			}
		}
	}

	// Register interest and observe that the log is emitted.
	{
		var interest diagnostics.Interest
		interest.SetMinSeverity(diagnostics.SeverityDebug)
		registerInterest(interest)
	}
	if err := log.VLogf(syslog.DebugVerbosity, format, 10); err != nil {
		t.Fatal(err)
	}
	expectedMsg := fmt.Sprintf(format, 10)
	checkoutput(t, sin, expectedMsg, syslog.InfoLevel-1)

	// Register empty interest and observe that severity resets to initial.
	registerInterest(diagnostics.Interest{})
	if err := log.VLogf(syslog.DebugVerbosity, format, 10); err != nil {
		t.Fatal(err)
	}
	_, err := sin.Read(nil, 0)
	if err, ok := err.(*zx.Error); !ok || err.Status != zx.ErrShouldWait {
		t.Fatal(err)
	}
}

func TestGlobalTagLimits(t *testing.T) {
	options := syslog.LogInitOptions{
		Writer: os.Stdout,
	}
	var tags [logger.MaxTags + 1]string
	for i := 0; i < len(tags); i++ {
		tags[i] = "a"
	}
	options.Tags = tags[:]
	if _, err := syslog.NewLogger(options); err == nil || !strings.Contains(err.Error(), "too many tags") {
		t.Fatalf("unexpected error: %s", err)
	}
	options.Tags = tags[:logger.MaxTags]
	var tag [logger.MaxTagLenBytes + 1]byte
	for i := 0; i < len(tag); i++ {
		tag[i] = 65
	}
	options.Tags[1] = string(tag[:])
	if _, err := syslog.NewLogger(options); err == nil || !strings.Contains(err.Error(), "tag too long") {
		t.Fatalf("unexpected error: %s", err)
	}
}

func TestLocalTagLimits(t *testing.T) {
	_, sin, log := setup(t)
	defer func() {
		if err := log.Close(); err != nil {
			t.Error(err)
		}
		if err := sin.Close(); err != nil {
			t.Error(err)
		}
	}()
	var tag [logger.MaxTagLenBytes + 1]byte
	for i := 0; i < len(tag); i++ {
		tag[i] = 65
	}
	if err := log.InfoTf(string(tag[:]), format, 10); err != nil {
		t.Fatal(err)
	}
	expectedMsg := fmt.Sprintf(format, 10)
	checkoutput(t, sin, expectedMsg, syslog.InfoLevel, string(tag[:logger.MaxTagLenBytes]))
}

func TestLogToWriterWhenSocketCloses(t *testing.T) {
	_, sin, log := setup(t, "gtag1", "gtag2")
	defer func() {
		if err := log.Close(); err != nil {
			t.Error(err)
		}
	}()
	if err := sin.Close(); err != nil {
		t.Fatal(err)
	}
	old := os.Stderr
	defer func() {
		os.Stderr = old
	}()

	f, err := os.Create(filepath.Join(t.TempDir(), "syslog-test"))
	if err != nil {
		t.Fatal(err)
	}
	defer func() {
		if err := f.Close(); err != nil {
			t.Error(err)
		}
	}()
	os.Stderr = f
	if err := log.InfoTf("local_tag", format, 10); err != nil {
		t.Fatal(err)
	}
	if err := f.Sync(); err != nil {
		t.Fatal(err)
	}
	expectedMsg := fmt.Sprintf("[0][gtag1, gtag2, local_tag] INFO: %s\n", fmt.Sprintf(format, 10))
	content, err := ioutil.ReadFile(f.Name())
	os.Stderr = old
	if err != nil {
		t.Fatal(err)
	}
	got := string(content)
	if !strings.HasSuffix(got, expectedMsg) {
		t.Fatalf("%q should have ended in %q", got, expectedMsg)
	}
}

func TestMessageLenLimit(t *testing.T) {
	_, sin, log := setup(t)
	defer func() {
		if err := log.Close(); err != nil {
			t.Error(err)
		}
		if err := sin.Close(); err != nil {
			t.Error(err)
		}
	}()
	// 1 for starting and ending null bytes.
	msgLen := int(logger.MaxDatagramLenBytes) - 32 - 1 - 1

	const stripped = '𠜎'
	// Ensure only part of stripped fits.
	msg := strings.Repeat("x", msgLen-(utf8.RuneLen(stripped)-1)) + string(stripped)
	switch err := log.Infof(msg).(type) {
	case *syslog.ErrMsgTooLong:
		if err.Msg != string(stripped) {
			t.Fatalf("unexpected truncation: %s", err.Msg)
		}
	default:
		t.Fatalf("unexpected error: %#v", err)
	}

	const ellipsis = "..."
	expectedMsg := msg[:msgLen-len(ellipsis)] + ellipsis
	checkoutput(t, sin, expectedMsg, syslog.InfoLevel)
}
