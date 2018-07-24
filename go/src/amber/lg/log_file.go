// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package lg

import (
	"fmt"
	"os"
	"path"
	"sync"
	"time"
)

type FileLogger struct {
	mu       sync.Mutex
	path     string
	file     *os.File
	buf      []byte
	nwritten uint64
	options  FileLoggerOptions
}

type FileLoggerOptions struct {
	MaxLogSize     uint64
	MaxOldLogFiles int
	Timestamp      bool
}

// NewFileLogger creates a new file-based logger using the provided path as the
// location of the files used. The logger uses multiple files to assist in log
// rotation. If the provided path is a pattern containing a "%d" the index of
// the log file will be placed there. If the pattern does not contain '%d', the
// name will be postfixed with log file indexes.
func NewFileLogger(path string) *FileLogger {
	return NewFileLoggerWithOptions(path, FileLoggerOptions{
		MaxLogSize:     2 * 1024 * 1024,
		MaxOldLogFiles: 5,
		Timestamp:      true,
	})
}

func NewFileLoggerWithOptions(path string, options FileLoggerOptions) *FileLogger {
	return &FileLogger{
		path:    path,
		file:    nil,
		options: options,
	}
}

func (l *FileLogger) Infof(format string, v ...interface{}) error {
	return l.Output(1, InfoLevel, fmt.Sprintf(format, v...))
}

func (l *FileLogger) Warnf(format string, v ...interface{}) error {
	return l.Output(1, WarningLevel, fmt.Sprintf(format, v...))
}

func (l *FileLogger) Errorf(format string, v ...interface{}) error {
	return l.Output(1, ErrorLevel, fmt.Sprintf(format, v...))
}

func (l *FileLogger) Fatalf(format string, v ...interface{}) {
	l.Output(1, FatalLevel, fmt.Sprintf(format, v...))
	os.Exit(1)
}

func (l *FileLogger) Output(callDepth int, logLevel LogLevel, s string) error {
	now := time.Now()

	l.mu.Lock()
	defer l.mu.Unlock()

	if l.file == nil {
		if err := l.openLocked(); err != nil {
			return err
		}
	}

	l.buf = l.buf[:0]
	if l.options.Timestamp {
		l.buf = now.AppendFormat(l.buf, time.Stamp)
		l.buf = append(l.buf, ' ')
	}
	l.buf = append(l.buf, logLevel.String()...)
	l.buf = append(l.buf, ": "...)
	l.buf = append(l.buf, s...)

	if len(s) == 0 || s[len(s)-1] != '\n' {
		l.buf = append(l.buf, '\n')
	}

	p := l.buf
	for len(p) > 0 {
		if l.nwritten+uint64(len(p)) >= l.options.MaxLogSize {
			if err := l.rotateLocked(); err != nil {
				return err
			}
		}

		n, err := l.file.Write([]byte(p))
		l.nwritten += uint64(n)
		if err != nil {
			return err
		}
		p = p[n:]
	}

	return nil
}

func (l *FileLogger) openLocked() error {
	// allow mkdir to fail non-fatally, the log file creation will error if the
	// path is inaccessible.
	os.MkdirAll(path.Dir(l.path), os.ModePerm)

	f, err := os.OpenFile(l.path, os.O_RDWR|os.O_CREATE|os.O_APPEND, 0666)
	if err != nil {
		return err
	}

	info, err := f.Stat()
	if err != nil {
		f.Close()
		return fmt.Errorf("couldn't stat log file: %s", l.path)
	}

	l.file = f
	l.nwritten = uint64(info.Size())

	if l.nwritten >= l.options.MaxLogSize {
		if err := l.rotateLocked(); err != nil {
			return err
		}
	}

	return nil
}

func (l *FileLogger) Close() error {
	l.mu.Lock()
	defer l.mu.Unlock()

	return l.closeLocked()
}

// This should be called with the mutex held.
func (l *FileLogger) closeLocked() error {
	if l.file != nil {
		err := l.file.Close()
		l.file = nil
		return err
	}

	return nil
}

func (l *FileLogger) Sync() error {
	l.mu.Lock()
	defer l.mu.Unlock()

	if l.file != nil {
		return l.file.Sync()
	} else {
		return nil
	}
}

// Roll will close the current log file, rename the file to indicate it is not
// the active file, and start writing to a new active file.
func (l *FileLogger) Rotate() error {
	l.mu.Lock()
	defer l.mu.Unlock()

	return l.rotateLocked()
}

func (l *FileLogger) rotateLocked() error {
	if err := l.closeLocked(); err != nil {
		return fmt.Errorf("error closing file %v", err)
	}

	files := []string{l.path}
	for i := 0; i < l.options.MaxOldLogFiles; i++ {
		files = append(files, fmt.Sprintf("%s.%d", l.path, i))
	}

	for i := len(files) - 1; i >= 1; i-- {
		src := files[i-1]
		dst := files[i]

		if _, err := os.Stat(src); os.IsNotExist(err) {
			continue
		}

		if err := os.Rename(src, dst); err != nil {
			// we can't rename, there is little point in try truncating
			return fmt.Errorf("rename failed, attempting to truncate current %v", err)
		}
	}

	f, err := os.Create(l.path)
	if err != nil {
		l.file = nil
		return fmt.Errorf("opening new log file failed %v", err)
	}

	l.file = f
	l.nwritten = 0

	return nil
}
