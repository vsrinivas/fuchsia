// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package daemon

import (
	"bytes"
	"fmt"
	"io"
	"log"
	"math/rand"
	"os"
	"path"
	"testing"
	"time"
)

type mockLogger struct {
	t        *testing.T
	fileSize int64
	*bytes.Buffer

	written int
	truncs  int
}

func (l *mockLogger) Open() error {
	l.Buffer = bytes.NewBuffer([]byte{})
	return nil
}

func (l *mockLogger) Ready() bool {
	return l.Buffer != nil
}

func (l *mockLogger) Write(b []byte) (int, error) {
	if l.written < 0 {
		l.t.Fatalf("File write after close")
	}

	if l.written > logSize {
		l.t.Fatalf("File should be truncated")
	}
	l.written += len(b)

	i, e := l.Buffer.Write(b)
	if e != nil {
		l.t.Fatalf("Error writing to buffer: %v", e)
	}
	return i, e
}

func (l *mockLogger) Seek(off int64, whence int) (int64, error) {
	if l.written < 0 {
		l.t.Fatalf("File seek after close")
	}

	if whence == io.SeekCurrent {
		l.t.Fatalf("Mock does not support SeekCurrent!")
	}

	consumed := l.written - l.Len()
	if whence == io.SeekEnd {
		off = int64(l.written) - (-off)
	}

	if off < int64(consumed) {
		l.t.Fatalf("Can't seek back this far, bytes consumed")
	}
	l.Next(int(off) - consumed)

	if whence == io.SeekEnd {
		return -off, nil
	} else {
		return off, nil
	}

	return 0, nil
}

func (l *mockLogger) Truncate(size int64) error {
	l.written = int(size)
	l.Buffer.Truncate(int(size))
	return nil
}

func (l *mockLogger) Roll() error {
	l.truncs++
	if l.written < 0 {
		l.t.Fatalf("File truncate after close")
	}
	if l.written != logSize {
		l.t.Fatalf("Unexpected call to truncate")
	}

	if l.written < 0 {
		l.t.Fatalf("Log roll after close")
	}
	return l.Truncate(0)
}

func (l *mockLogger) Stat() (os.FileInfo, error) {
	if l.written < 0 {
		l.t.Fatalf("File stat after close")
	}
	return &mockStat{size: int64(l.written)}, nil
}

func (l *mockLogger) Close() error {
	l.Buffer.Truncate(0)
	l.written = -1
	return nil
}

func TestLogWrite(t *testing.T) {
	mock := mockLogger{
		t:        t,
		fileSize: logSize + 1}
	Log = log.New(newLogWriter(&mock), "", log.Ldate|log.Ltime)

	msg := "Hey Fuchsia!"
	Log.Printf(msg)

	content := make([]byte, mock.written)
	mock.Read(content)

	// log.Printf appends a newline, so we slice that off to compare
	tail := string(content[mock.written-len(msg)-1 : mock.written-1])
	if msg != tail {
		t.Fatalf("Log content did not match '%s' != '%s'", msg, tail)
	}
}

func TestLogRotation(t *testing.T) {
	mock := mockLogger{
		t:        t,
		fileSize: logSize + 1}
	Log = log.New(newLogWriter(&mock), "", log.Ldate|log.Ltime)

	msg := "Hey Fuchsia!"
	Log.Printf(msg)
	msgSize := mock.written

	rollIters := logSize/msgSize + 1
	for i := 0; i < rollIters; i++ {
		Log.Printf(msg)
	}

	if mock.written != mock.Len() {
		t.Fatalf("Internal buffer account doesn't match ours")
	}

	if mock.written != ((rollIters+1)*msgSize)%logSize {
		t.Fatalf("Final buffer size is %d expected %d", mock.written,
			((rollIters+1)*msgSize)%logSize)
	}

	if mock.truncs > 1 {
		t.Fatalf("Too many truncates %d expected 1", mock.truncs)
	}
}

func TestLargeLog(t *testing.T) {
	mock := mockLogger{
		t:        t,
		fileSize: logSize + 1}
	Log = log.New(newLogWriter(&mock), "", 0)
	msg := make([]byte, logSize*5/2)

	Log.Printf(string(msg))

	if mock.written != mock.Len() {
		t.Fatalf("Internal buffer account doesn't match ours")
	}

	if expected := (len(msg) + 1) % logSize; mock.written != expected {
		t.Fatalf("Final buffer size is %d expected %d", mock.written, expected)
	}

	if mock.truncs != 2 {
		t.Fatalf("Unexpected number of truncations: %d instead of %d",
			mock.truncs, 2)
	}
}

func TestLogRoll(t *testing.T) {
	rand.Seed(time.Now().UnixNano())
	namePattern := fmt.Sprintf("logger-test-%d-%s.log", rand.Uint32(), "%d")
	logPattern := path.Join(os.TempDir(), namePattern)

	logWriter := newLogWriter(NewFileLogger(logPattern))
	Log = log.New(logWriter, "", log.Ldate|log.Ltime)
	defer os.Remove(fmt.Sprintf(logPattern, 1))

	msg := "Hey Fuchsia!"
	Log.Println(msg)
	msgSize, _ := logWriter.Stat()

	writeIters := int(logSize/msgSize.Size() - 1)
	// set i to 1 since we already wrote once above
	for i := 1; i < writeIters; i++ {
		Log.Println(msg)
	}

	fInfo, err := os.Lstat(fmt.Sprintf(logPattern, 1))
	if err != nil {
		t.Fatalf("Stat of log file failed %v", err)
	}
	if fInfo.Size() != int64(writeIters)*msgSize.Size() {
		t.Fatalf("File size is unexpected %d %d", fInfo.Size(),
			int64(writeIters)*msgSize.Size())
	}

	toRoll := int((logSize-fInfo.Size())/msgSize.Size() + 1)

	fInfo, err = os.Lstat(fmt.Sprintf(logPattern, 2))
	if err == nil {
		t.Fatalf("Second log file exists unexpectedly")
	}

	for i := 0; i < toRoll; i++ {
		Log.Println(msg)
	}
	writeIters += toRoll
	defer os.Remove(fmt.Sprintf(logPattern, 2))

	fInfo, err = os.Lstat(fmt.Sprintf(logPattern, 1))
	if err != nil {
		t.Fatalf("Stat of log file failed %v", err)
	}

	if expected := (int64(writeIters) * msgSize.Size()) % logSize; fInfo.Size() != expected {
		t.Fatalf("Active log size incorrect, expected %d by found %d",
			expected, fInfo.Size())
	}

	fInfo, err = os.Lstat(fmt.Sprintf(logPattern, 2))
	if err != nil {
		t.Fatalf("Stat of log file failed %v", err)
	}
	if fInfo.Size() != logSize {
		t.Fatalf("Older log file size incorrect, expected %d but found %d",
			logSize, fInfo.Size())
	}

	doubleRoll := logSize*2/int(msgSize.Size()) + 1
	doubleRoll -= writeIters

	for i := 0; i < doubleRoll; i++ {
		Log.Println(msg)
	}

	writeIters += doubleRoll

	fInfo, err = os.Lstat(fmt.Sprintf(logPattern, 1))
	if err != nil {
		t.Fatalf("Stat of log file failed %v", err)
	}

	if expected := (int64(writeIters) * msgSize.Size()) % logSize; fInfo.Size() != expected {
		t.Fatalf("Active log size incorrect, expected %d by found %d",
			expected, fInfo.Size())
	}

	fInfo, err = os.Lstat(fmt.Sprintf(logPattern, 2))
	if err != nil {
		t.Fatalf("Stat of log file failed %v", err)
	}
	if fInfo.Size() != logSize {
		t.Fatalf("Older log file size incorrect, expected %d but found %d",
			logSize, fInfo.Size())
	}

	logWriter.Close()
}

type mockStat struct {
	size int64
}

func (m *mockStat) Name() string {
	return ""
}

func (m *mockStat) Size() int64 {
	return m.size
}

func (m *mockStat) Mode() os.FileMode {
	return os.ModePerm
}

func (m *mockStat) ModTime() time.Time {
	return time.Now()
}

func (m *mockStat) IsDir() bool {
	return false
}

func (m *mockStat) Sys() interface{} {
	return nil
}
