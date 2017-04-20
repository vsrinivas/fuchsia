// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"apps/netstack/watcher"
	"encoding/binary"
	"fmt"
	"log"
	"os"
	"syscall"
	"syscall/mx/mxio"
)

// TODO(tkilbourn): reuse netstack2's ethernet client lib (NET-33)
type ethinfo struct {
	features uint32
	mtu      uint32
	mac      [6]byte
	_        [2]byte
	_        [12]uint32
}

const ioctlKind = mxio.IoctlKindDefault
const ioctlFamilyETH = 0x20
const (
	ioctlOpGetInfo = 0
)

const ethFeatureWlan = 0x01

func main() {
	log.SetFlags(0)
	log.SetPrefix("wlanstack: ")
	log.Print("started")

	const ethdir = "/dev/class/ethernet"
	w, err := watcher.NewWatcher(ethdir)
	if err != nil {
		log.Fatalf("ethernet: %v", err)
	}
	log.Printf("watching for wlan devices")

	for name := range w.C {
		path := ethdir + "/" + name
		if err := tryAddEth(path); err != nil {
			log.Printf("failed to add wlan device %s: %v", path, err)
		}
	}
}

func tryAddEth(path string) error {
	log.Printf("trying ethernet device %q", path)

	f, err := os.Open(path)
	if err != nil {
		return fmt.Errorf("wlan: client open: %v", err)
	}
	m := syscall.MXIOForFD(int(f.Fd()))

	num := mxio.IoctlNum(mxio.IoctlKindDefault, ioctlFamilyETH, ioctlOpGetInfo)
	res := make([]byte, 64)
	if _, err := m.Ioctl(num, nil, res); err != nil {
		return fmt.Errorf("IOCTL_ETHERNET_GET_INFO: %v", err)
	}

	info := ethinfo{
		features: binary.LittleEndian.Uint32(res),
		mtu:      binary.LittleEndian.Uint32(res[4:]),
	}
	copy(info.mac[:], res[8:])

	if info.features&ethFeatureWlan != 0 {
		log.Printf("found wlan device %q", path)
	}
	return nil
}
