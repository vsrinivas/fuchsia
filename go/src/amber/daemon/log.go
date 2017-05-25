// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package daemon

import (
	"errors"
	"fmt"
	"io"
	"log"
	"os"
	"path"
	"strings"
)

var ErrNoLogFile = errors.New("amber: log file could not be opened.")
var logFile = "/data/amber/log_%d.txt"
var Log = log.New(newLogWriter(NewFileLogger(logFile)), "", log.Ldate|log.Ltime)

const logSize = 2 * 1024 * 1024

type logWriter struct {
	Logger
}

type Logger interface {
	io.ReadWriter
	io.Closer
	io.Seeker
	Open() error
	Truncate(int64) error
	Stat() (os.FileInfo, error)
	Ready() bool
	Roll() error
}

func newLogWriter(logger Logger) *logWriter {
	err := logger.Open()
	if err != nil {
		log.Printf("logwriter: log storage failed to open %v\n", err)
	}
	return &logWriter{logger}
}

func (l *logWriter) Write(msg []byte) (int, error) {
	origSize := len(msg)
	var err error = nil

	for len(msg) > 0 {
		if !l.Ready() {
			log.Println(string(msg))
			return 0, ErrNoLogFile
		}

		info, err := l.Stat()
		if err != nil {
			l.Close()
			log.Println("logwriter: couldn't stat log file, turning logging off.")
			break
		}

		if info.Size() >= logSize {
			if err = l.Roll(); err != nil {
				break
			}
		}

		writeSize := int64(len(msg))

		if writeSize > logSize-info.Size() {
			writeSize = logSize - info.Size()
		}

		_, err = l.Logger.Write(msg[:writeSize])
		if err != nil {
			break
		}

		msg = msg[writeSize:]
	}

	return origSize - len(msg), err
}

type FileLogger struct {
	string
	*os.File
}

// NewFileLogger creates a new file-based logger using the provided path as the
// location of the files used. The logger uses multiple files to assist in log
// rotation. If the provided path is a pattern containing a "%d" the index of
// the log file will be placed there. If the pattern does not contain '%d', the
// name will be postfixed with log file indexes.
func NewFileLogger(path string) *FileLogger {
	if !strings.Contains(path, "%d") {
		path += "-%d"
	}
	return &FileLogger{path, nil}
}

func (fl *FileLogger) Open() error {
	logPath := fmt.Sprintf(fl.string, 1)
	if e := os.MkdirAll(path.Dir(logPath), os.ModePerm); e != nil {
		log.Printf("filelog: log directory parent creation failed %v\n", e)
		return e
	}

	f, e := os.OpenFile(logPath, os.O_RDWR|os.O_CREATE|os.O_APPEND, 0666)
	if e == nil {
		fl.File = f
	} else {
		log.Printf("filelog: error opening file %v\n", e)
	}
	return e
}

func (fl *FileLogger) Ready() bool {
	return fl.File != nil
}

func (fl *FileLogger) Close() error {
	if fl.File != nil {
		err := fl.File.Close()
		fl.File = nil
		return err
	} else {
		return nil
	}
}

// Roll will close the current log file, rename the file to indicate it is not
// the active file, and start writing to a new active file.
func (fl *FileLogger) Roll() error {
	if e := fl.File.Close(); e != nil {
		fmt.Printf("fileLog: error closing file %v\n", e)
	}

	if e := os.Rename(fmt.Sprintf(fl.string, 1), fmt.Sprintf(fl.string, 2)); e != nil {
		// we can't rename, there is little point in try truncating
		fmt.Printf("filelog: rename failed, attempting to truncate current %v\n", e)
	}

	f, e := os.Create(fmt.Sprintf(fl.string, 1))
	if e != nil {
		fl.File = nil
		fmt.Printf("filelog: opening new log file failed %v\n", e)
		return e
	}

	fl.File = f
	return nil
}
