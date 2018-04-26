// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"strings"
	"syscall/zx"
	"syslog/logger"
	"testing"
)

var (
	Pid = uint64(os.Getpid())
)

func TestLogSimpleWithWriter(t *testing.T) {
	options := logger.GetDefaultInitOptions()
	tmpFile, err := ioutil.TempFile("", "test")
	if err != nil {
		t.Fatal(err)
	}
	defer os.Remove(tmpFile.Name())
	options.ConsoleWriter = tmpFile
	logger, err := logger.NewLogger(options)
	if err != nil {
		t.Fatal(err)
	}
	logger.Infof("integer: %d", 10)
	tmpFile.Sync()
	tmpFile.Seek(0, 0)
	var buf bytes.Buffer
	io.Copy(&buf, tmpFile)
	expected := "INFO: integer: 10\n"
	got := buf.String()
	if !strings.HasSuffix(got, expected) {
		t.Errorf("%q should have ended in %q", got, expected)
	}
	if !strings.Contains(got, fmt.Sprintf("[%d]", Pid)) {
		t.Errorf("%q should contains %d", got, Pid)
	}
}
func setup(t *testing.T, tags ...string) (zx.Socket, *logger.Logger) {
	options := logger.GetDefaultInitOptions()
	options.Tags = tags
	sin, sout, err := zx.NewSocket(logger.SOCKET_DATAGRAM)
	if err != nil {
		t.Fatal(err)
	}
	options.LogServiceChannel = &sout
	log, err := logger.NewLogger(options)
	if err != nil {
		t.Fatal(err)
	}
	return sin, log
}

func checkoutput(t *testing.T, sin zx.Socket, expectedMsg string, severity logger.LogLevel, tags ...string) {
	var data [logger.SOCKET_BUFFER_LEN]byte
	if n, err := sin.Read(data[:], 0); err != nil {
		t.Fatal(err)
	} else {
		if n <= 32 {
			t.Fatalf("got invalid data: %v", data[:n])
		}
		got_pid := binary.LittleEndian.Uint64(data[0:8])
		got_tid := binary.LittleEndian.Uint64(data[8:16])
		got_time := binary.LittleEndian.Uint64(data[16:24])
		got_severity := int32(binary.LittleEndian.Uint32(data[24:28]))
		got_dropped_logs := int32(binary.LittleEndian.Uint32(data[28:32]))

		if Pid != got_pid {
			t.Errorf("pid error, got: %d, want: %d", got_pid, Pid)
		}

		if 0 != got_tid {
			t.Errorf("tid error, got: %d, want: %d", got_tid, 0)
		}

		if int32(severity) != got_severity {
			t.Errorf("severity error, got: %d, want: %d", got_severity, severity)
		}

		if got_time <= 0 {
			t.Errorf("time %d should be greater than zero", got_time)
		}

		if 0 != got_dropped_logs {
			t.Errorf("dropped logs error, got: %d, want: %d", got_dropped_logs, 0)
		}
		pos := 32
		for i, tag := range tags {
			length := len(tag)
			if data[pos] != byte(length) {
				t.Fatalf("tag iteration %d: expected data to be %d at pos %d, got %d", i, length, pos, data[pos])
			}
			pos = pos + 1
			got_tag := string(data[pos : pos+length])
			if tag != got_tag {
				t.Fatalf("tag iteration %d: expected tag %q , got %q", i, tag, got_tag)
			}
			pos = pos + length
		}

		if data[pos] != 0 {
			t.Fatalf("byte before msg start should be zero, got: %d, %v", data[pos], data[32:n])
		}

		msg_got := string(data[pos+1 : n-1])
		if expectedMsg != msg_got {
			t.Fatalf("expected msg:%q, got %q", expectedMsg, msg_got)
		}
		if data[n-1] != 0 {
			t.Fatalf("last byte should be zero, got: %d, %v", data[pos], data[32:n])
		}
	}
}

// Test simple logging with socket
func TestLogSimple(t *testing.T) {
	sin, log := setup(t)
	format := "integer: %d"
	log.Infof(format, 10)
	expectedMsg := fmt.Sprintf(format, 10)
	checkoutput(t, sin, expectedMsg, logger.InfoLevel)
}

func TestLogWithLocalTag(t *testing.T) {
	sin, log := setup(t)
	format := "integer: %d"
	log.InfoTf("local_tag", format, 10)
	expectedMsg := fmt.Sprintf(format, 10)
	checkoutput(t, sin, expectedMsg, logger.InfoLevel, "local_tag")
}

func TestLogWithGlobalTags(t *testing.T) {
	sin, log := setup(t, "gtag1", "gtag2")
	format := "integer: %d"
	log.InfoTf("local_tag", format, 10)
	expectedMsg := fmt.Sprintf(format, 10)
	checkoutput(t, sin, expectedMsg, logger.InfoLevel, "gtag1", "gtag2", "local_tag")
}

func TestLoggerSeverity(t *testing.T) {
	sin, log := setup(t)
	format := "integer: %d"
	log.SetSeverity(logger.WarningLevel)
	log.Infof(format, 10)
	if n, err := sin.Read(make([]byte, 0), 0); err != nil {
		t.Fatal(err)
	} else if n != 0 {
		t.Fatalf("expected n: 0, got %d", n)
	}
	log.Warnf(format, 10)
	expectedMsg := fmt.Sprintf(format, 10)
	checkoutput(t, sin, expectedMsg, logger.WarningLevel)
}

func TestLoggerVerbosity(t *testing.T) {
	sin, log := setup(t)
	format := "integer: %d"
	log.VLogf(2, format, 10)
	if n, err := sin.Read(make([]byte, 0), 0); err != nil {
		t.Fatal(err)
	} else if n != 0 {
		t.Fatalf("expected n: 0, got %d", n)
	}
	log.SetVerbosity(2)
	log.VLogf(2, format, 10)
	expectedMsg := fmt.Sprintf(format, 10)
	checkoutput(t, sin, expectedMsg, logger.LogLevel(-2))
}

func TestGlobalTagLimits(t *testing.T) {
	options := logger.GetDefaultInitOptions()
	options.ConsoleWriter = os.Stdout
	tags := [logger.MAX_GLOBAL_TAGS + 1]string{}
	for i := 0; i < len(tags); i++ {
		tags[i] = "a"
	}
	options.Tags = tags[:]
	if _, err := logger.NewLogger(options); err != logger.ErrInvalidArg {
		t.Fatalf("expected error 'ErrInvalidArg' got: %s", err)
	}
	options.Tags = tags[0:logger.MAX_GLOBAL_TAGS]
	var tag [logger.MAX_TAG_LEN + 1]byte
	for i := 0; i < len(tag); i++ {
		tag[i] = 65
	}
	options.Tags[1] = string(tag[:])
	if _, err := logger.NewLogger(options); err != logger.ErrInvalidArg {
		t.Fatalf("expected error 'ErrInvalidArg' got: %s", err)
	}
}

func TestLocalTagLimits(t *testing.T) {
	sin, log := setup(t)
	format := "integer: %d"
	var tag [logger.MAX_TAG_LEN + 1]byte
	for i := 0; i < len(tag); i++ {
		tag[i] = 65
	}
	log.InfoTf(string(tag[:]), format, 10)
	expectedMsg := fmt.Sprintf(format, 10)
	checkoutput(t, sin, expectedMsg, logger.InfoLevel, string(tag[:logger.MAX_TAG_LEN]))
}

func TestFallback(t *testing.T) {
	sin, log := setup(t, "gtag1", "gtag2")
	sin.Close()
	old := os.Stderr
	f, err := ioutil.TempFile("", "stderr")
	if err != nil {
		t.Fatal(err)
	}
	os.Stderr = f
	defer func() {
		os.Stderr = old
		os.Remove(f.Name())
	}()
	log.ActivateFallbackMode()
	format := "integer: %d"
	log.InfoTf("local_tag", format, 10)
	expectedMsg := fmt.Sprintf("[0][gtag1, gtag2, local_tag] INFO: %s\n", fmt.Sprintf(format, 10))
	content, err := ioutil.ReadFile(f.Name())
	if err != nil {
		t.Fatal(err)
	}
	got := string(content)
	if !strings.HasSuffix(got, expectedMsg) {
		t.Fatalf("%q should have ended in %q", got, expectedMsg)
	}
}

func TestFallbackWhenSocketCloses(t *testing.T) {
	sin, log := setup(t, "gtag1", "gtag2")
	sin.Close()
	old := os.Stderr
	f, err := ioutil.TempFile("", "stderr")
	if err != nil {
		t.Fatal(err)
	}
	os.Stderr = f
	defer func() {
		os.Stderr = old
		os.Remove(f.Name())
	}()
	format := "integer: %d"
	log.InfoTf("local_tag", format, 10)
	expectedMsg := fmt.Sprintf("[0][gtag1, gtag2, local_tag] INFO: %s\n", fmt.Sprintf(format, 10))
	content, err := ioutil.ReadFile(f.Name())
	if err != nil {
		t.Fatal(err)
	}
	got := string(content)
	if !strings.HasSuffix(got, expectedMsg) {
		t.Fatalf("%q should have ended in %q", got, expectedMsg)
	}
}

func TestMessageLenLimit(t *testing.T) {
	sin, log := setup(t)
	msgLen := logger.SOCKET_BUFFER_LEN - 32 - 1 - 1 // 1 for starting and ending null bytes
	format := fmt.Sprintf("%%0%ddstart", msgLen-3)
	if err := log.Infof(format, 10); err != nil {
		if err2 := logger.ToErrMsgTooLong(err); err2 == nil {
			t.Fatal(err)
		} else {
			if err2.Msg != "start" {
				t.Fatalf("expected 'start' got %q", err2.Msg)
			}
		}
	}
	expectedMsg := fmt.Sprintf(format, 10)
	expectedMsgBytes := []byte(expectedMsg)
	expectedMsgBytes = expectedMsgBytes[:msgLen]
	copy(expectedMsgBytes[msgLen-3:msgLen], "...")
	expectedMsg = string(expectedMsgBytes[:]) // "<0000...ntimes>10..."
	checkoutput(t, sin, expectedMsg, logger.InfoLevel)
}
