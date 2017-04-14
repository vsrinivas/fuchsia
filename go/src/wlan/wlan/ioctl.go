// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package wlan

import (
	"encoding/binary"
	"fmt"
	"syscall/mx"
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

const ioctlFamilyETH = 0x20
const (
	ioctlOpEthGetInfo = 0
)

const ethFeatureWlan = 0x01

const ioctlFamilyWLAN = 0x24 // IOCTL_FAMILY_WLAN
const (
	ioctlOpWlanGetChannel = 0 // IOCTL_WLAN_GET_CHANNEL,        IOCTL_KIND_GET_HANDLE
)

func ioctlEthGetInfo(m mxio.MXIO) (info ethinfo, err error) {
	num := mxio.IoctlNum(mxio.IoctlKindDefault, ioctlFamilyETH, ioctlOpEthGetInfo)
	res := make([]byte, 64)
	if _, err := m.Ioctl(num, nil, res); err != nil {
		return info, fmt.Errorf("IOCTL_ETHERNET_GET_INFO: %v", err)
	}
	info = ethinfo{
		features: binary.LittleEndian.Uint32(res),
		mtu:      binary.LittleEndian.Uint32(res[4:]),
	}
	copy(info.mac[:], res[8:])
	return info, nil
}

func ioctlWlanGetChannel(m mxio.MXIO) (ch mx.Handle, err error) {
	num := mxio.IoctlNum(mxio.IoctlKindGetHandle, ioctlFamilyWLAN, ioctlOpWlanGetChannel)
	h, err := m.Ioctl(num, nil, nil)
	if err != nil {
		return ch, fmt.Errorf("IOCTL_WLAN_GET_CHANNEL: %v", err)
	}
	if len(h) < 1 {
		return ch, fmt.Errorf("IOCTL_WLAN_GET_CHANNEL: received no handles")
	}
	ch = h[0]
	return ch, nil
}
