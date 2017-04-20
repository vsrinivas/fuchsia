// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package watcher

import (
	"os"

	"syscall"
	"syscall/mx"
	"syscall/mx/mxio"
)

// Watcher watches a directory, send the name of any new file to
// the channel C when it is seen.
type Watcher struct {
	C   chan string // recieves new file names
	Err error

	f *os.File
	h mx.Channel
}

// NewWatcher creates a watcher on the directory dir.
func NewWatcher(dir string) (*Watcher, error) {
	m, err := syscall.OpenPath(dir, syscall.O_RDONLY, 0)
	if err != nil {
		return nil, err
	}
	h, err := ioctlDeviceWatchDir(m)
	if err != nil {
		return nil, err
	}
	w := &Watcher{
		C: make(chan string),
		f: os.NewFile(uintptr(syscall.OpenMXIO(m)), dir + " watcher"),
		h: mx.Channel{Handle: h},
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
		name, err := w.wait()
		if err != nil {
			w.Err = err
			w.Stop()
			return
		}
		w.C <- name
	}
}

func (w *Watcher) wait() (string, error) {
	_, err := w.h.Handle.WaitOne(mx.SignalChannelReadable|mx.SignalChannelPeerClosed, mx.TimensecInfinite)
	if err != nil {
		return "", err
	}
	const NAME_MAX = 255
	var name [NAME_MAX]byte
	n, _, err := w.h.Read(name[:], nil, 0)
	if err != nil {
		return "", err
	}
	return string(name[:n]), nil
}

type errorString string

func (e errorString) Error() string { return string(e) }

func ioctlDeviceWatchDir(m mxio.MXIO) (h mx.Handle, err error) {
	const ioctlFamilyDevice = 0x01
	const op = 1 // IOCTL_DEVICE_WATCH_DIR
	num := mxio.IoctlNum(mxio.IoctlKindGetHandle, ioctlFamilyDevice, op)
	hs, err := m.Ioctl(num, nil, nil)
	if err != nil {
		return 0, errorString("IOCTL_DEVICE_WATCH_DIR: " + err.Error())
	}
	if len(hs) != 1 {
		return 0, errorString("IOCTL_DEVICE_WATCH_DIR: bad number of handles")
	}
	return hs[0], nil
}
