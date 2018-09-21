// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package watcher

import (
	"errors"
	"os"

	"syscall"
	"syscall/zx"
	"syscall/zx/fdio"
	"syscall/zx/fidl"
	"syscall/zx/io"
	"syscall/zx/zxwait"
)

// Watcher watches a directory, send the name of any new file to
// the channel C when it is seen.
type Watcher struct {
	C   chan string // recieves new file names
	Err error

	f *os.File
	h zx.Channel
}

// NewWatcher creates a watcher on the directory dir.
func NewWatcher(dir string) (*Watcher, error) {
	m, err := syscall.OpenPath(dir, syscall.O_RDONLY, 0)
	if err != nil {
		return nil, err
	}
	h, err := VFSWatchDir(m)
	if err != nil {
		return nil, err
	}
	w := &Watcher{
		C: make(chan string),
		f: os.NewFile(uintptr(syscall.OpenFDIO(m)), dir+" watcher"),
		h: zx.Channel(h),
	}
	go w.start()
	return w, nil
}

// Stop stops a watcher. After it returns, any error that occurred
// while watching is in the Err field.
func (w *Watcher) Stop() {
	w.h.Close()
	w.f.Close()
	close(w.C)
}

func (w *Watcher) start() {
	names, err := w.f.Readdirnames(-1)
	if err != nil {
		w.Err = err
		w.Stop()
		return
	}
	for _, name := range names {
		w.C <- name
	}
	for {
		const opAdded = 1
		name, op, err := w.wait()
		if err != nil {
			w.Err = err
			w.Stop()
			return
		}
		if op == opAdded {
			w.C <- name
		}
	}
}

func (w *Watcher) wait() (string, uint, error) {
	_, err := zxwait.Wait(*w.h.Handle(), zx.SignalChannelReadable|zx.SignalChannelPeerClosed, zx.TimensecInfinite)
	if err != nil {
		return "", 0, err
	}
	const NAME_MAX = 255
	const MSG_MAX = NAME_MAX + 2
	var msg [MSG_MAX]byte
	n, _, err := w.h.Read(msg[:], nil, 0)
	if err != nil {
		return "", 0, err
	}
	if (n <= 2) || (n != uint32(msg[1]+2)) {
		// malformed message
		return "", 0, errors.New("watcher: malformed message")
	}

	if msg[0] != io.WatchEventAdded {
		// TODO(smklein): Support other watch events
		return "", 0, errors.New("watcher: Invalid event")
	}
	return string(msg[2 : 2+msg[1]]), uint(msg[0]), nil
}

type errorString string

func (e errorString) Error() string { return string(e) }

func VFSWatchDir(m fdio.FDIO) (h zx.Handle, err error) {
	c1, c2, err := zx.NewChannel(0)
	if err != nil {
		return 0, errorString("VFS_WATCH_DIR: " + err.Error())
	}

	dirChan := zx.Channel(m.Handles()[0])
	dir := io.DirectoryInterface(fidl.InterfaceRequest{Channel: dirChan})
	status, err := dir.Watch(io.WatchMaskAdded, 0, c1)
	if err != nil {
		c2.Close()
		return 0, errorString("VFS_WATCH_DIR (IO error): " + err.Error())
	} else if status != zx.ErrOk {
		c2.Close()
		return 0, zx.Error{Status: status, Text: "VFS_WATCH_DIR (Server error)"}
	}
	return zx.Handle(c2), nil
}
