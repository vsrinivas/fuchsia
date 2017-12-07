// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"encoding/binary"
	"errors"
	"fmt"
	"os"
	"sync"

	"syscall"
	"syscall/zx"
	"syscall/zx/fdio"
)

type status int

const (
	running status = iota
	stopped
)

// Watcher watches a directory, send the name of any new file to
// the channel C when it is seen.
type Watcher struct {
	C   chan string // recieves new file names
	Err error
	f   *os.File
	h   zx.Channel
	s   status

	// Guards s
	lock sync.Mutex
}

// NewWatcher creates a watcher on the directory dir.
func NewWatcher(dir string) (*Watcher, error) {
	m, err := syscall.OpenPath(dir, syscall.O_RDONLY, 0)
	if err != nil {
		return nil, err
	}
	h, err := ioctlVFSWatchDir(m)
	if err != nil {
		return nil, err
	}
	w := &Watcher{
		C: make(chan string),
		f: os.NewFile(uintptr(syscall.OpenFDIO(m)), dir+" watcher"),
		h: zx.Channel{Handle: h},
		s: stopped,
	}
	w.start()
	return w, nil
}

// Stop stops a watcher. After it returns, any error that occurred
// while watching is in the Err field.
func (w *Watcher) Stop() {
	w.lock.Lock()
	defer w.lock.Unlock()
	if w.s == stopped {
		return
	}
	w.s = stopped
	w.h.Close()
	w.f.Close()
}

func (w *Watcher) start() {
	w.lock.Lock()
	w.s = running
	w.lock.Unlock()
	names, err := w.f.Readdirnames(-1)
	if err != nil {
		w.Err = err
		w.Stop()
		close(w.C)
		return
	}
	go func() {
		for _, name := range names {
			w.C <- name
		}

		for {
			const opAdded = 1
			name, op, err := w.wait()
			if err != nil {
				w.Err = err
				w.Stop()
				close(w.C)
				return
			}
			if op == opAdded {
				w.C <- name
			}
		}
	}()
}

func (w *Watcher) wait() (string, uint, error) {
	_, err := w.h.Handle.WaitOne(zx.SignalChannelReadable|zx.SignalChannelPeerClosed, zx.TimensecInfinite)
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
	if (n <= 2) || (n != uint32(msg[1])+2) {
		// malformed message
		return "", 0, errors.New("watcher: malformed message")
	}

	if msg[0] != fdio.VFSWatchEventAdded {
		// TODO(smklein): Support other watch events
		return "", 0, errors.New("watcher: Invalid event")
	}
	return string(msg[2 : 2+uint16(msg[1])]), uint(msg[0]), nil
}

func ioctlVFSWatchDir(m fdio.FDIO) (h zx.Handle, err error) {

	c1, c2, err := zx.NewChannel(0)
	if err != nil {
		return 0, fmt.Errorf("IOCTL_VFS_WATCH_DIR: %s", err)
	}
	msg := fdio.VFSWatchDirRequest{H: c1.Handle, Mask: fdio.VFSWatchMaskAdded, Options: 0}

	buf := new(bytes.Buffer)
	// LE for our arm64 and amd64 archs
	err = binary.Write(buf, binary.LittleEndian, msg)
	if err != nil {
		return 0, fmt.Errorf("binary.Write failed: %s", err)
	}

	_, err = m.Ioctl(fdio.IoctlVFSWatchDir, buf.Bytes(), nil)
	if err != nil {
		return 0, fmt.Errorf("IOCTL_VFS_WATCH_DIR:  %s", err)
	}
	return c2.Handle, nil
}
