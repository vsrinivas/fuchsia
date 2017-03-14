// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package eth

import (
	"encoding/binary"
	"fmt"
	"syscall/mx"
	"syscall/mx/mxio"
)

type ethinfo struct {
	features uint32
	mtu      uint32
	mac      [6]byte
	_        [2]byte
	_        [12]uint32
} // eth_info_t

type ethfifos struct {
	// fifo handles
	tx mx.Handle
	rx mx.Handle
	// maximum number of items in fifos
	txDepth uint32
	rxDepth uint32
} // eth_fifos_t

const ioctlKind = mxio.IoctlKindDefault
const ioctlFamilyETH = 0x20 // IOCTL_FAMILY_ETH
const (
	ioctlOpGetInfo       = 0 // IOCTL_ETHERNET_GET_INFO,        IOCTL_KIND_DEFAULT
	ioctlOpGetFifos      = 1 // IOCTL_ETHERNET_GET_FIFOS,       IOCTL_KIND_GET_TWO_HANDLES
	ioctlOpSetIobuf      = 2 // IOCTL_ETHERNET_SET_IOBUF,       IOCTL_KIND_SET_HANDLE
	ioctlOpStart         = 3 // IOCTL_ETHERNET_START,           IOCTL_KIND_DEFAULT
	ioctlOpStop          = 4 // IOCTL_ETHERNET_STOP,            IOCTL_KIND_DEFAULT
	ioctlOpTXListenStart = 5 // IOCTL_ETHERNET_TX_LISTEN_START, IOCTL_KIND_DEFAULT
	ioctlOpTXListenStop  = 6 // IOCTL_ETHERNET_TX_LISTEN_STOP,  IOCTL_KIND_DEFAULT
)

func ioctlGetInfo(m mxio.MXIO) (info ethinfo, err error) {
	num := mxio.IoctlNum(mxio.IoctlKindDefault, ioctlFamilyETH, ioctlOpGetInfo)
	res := make([]byte, 64)
	if _, err := m.Ioctl(num, nil, res); err != nil {
		return info, fmt.Errorf("IOCTL_ETHERNET_GET_INFO: %v", err)
	}
	info.features = binary.LittleEndian.Uint32(res)
	info.mtu = binary.LittleEndian.Uint32(res[4:])
	copy(info.mac[:], res[8:])
	return info, nil
}

func ioctlGetFifos(m mxio.MXIO) (fifos ethfifos, err error) {
	num := mxio.IoctlNum(mxio.IoctlKindGetTwoHandles, ioctlFamilyETH, ioctlOpGetFifos)
	res := make([]byte, 8)
	h, err := m.Ioctl(num, nil, res)
	if err != nil {
		return fifos, fmt.Errorf("IOCTL_ETHERNET_GET_FIFOS: %v", err)
	}
	if len(res) != 8 {
		return fifos, fmt.Errorf("IOCTL_ETHERNET_GET_FIFOS: bad length: %d", len(res))
	}
	fifos.tx = h[0]
	fifos.rx = h[1]
	fifos.txDepth = binary.LittleEndian.Uint32(res)
	fifos.rxDepth = binary.LittleEndian.Uint32(res[4:])
	return fifos, nil
}

func ioctlSetIobuf(m mxio.MXIO, h mx.Handle) error {
	num := mxio.IoctlNum(mxio.IoctlKindSetHandle, ioctlFamilyETH, ioctlOpSetIobuf)
	err := m.IoctlSetHandle(num, h)
	if err != nil {
		return fmt.Errorf("IOCTL_ETHERNET_SET_IOBUF: %v", err)
	}
	return nil
}

func ioctlStart(m mxio.MXIO) error {
	num := mxio.IoctlNum(mxio.IoctlKindDefault, ioctlFamilyETH, ioctlOpStart)
	_, err := m.Ioctl(num, nil, nil)
	if err != nil {
		return fmt.Errorf("IOCTL_ETHERNET_START: %v", err)
	}
	return nil
}

func ioctlStop(m mxio.MXIO) error {
	num := mxio.IoctlNum(mxio.IoctlKindDefault, ioctlFamilyETH, ioctlOpStop)
	_, err := m.Ioctl(num, nil, nil)
	if err != nil {
		return fmt.Errorf("IOCTL_ETHERNET_STOP: %v", err)
	}
	return nil
}

func ioctlTXListenStart(m mxio.MXIO) error {
	num := mxio.IoctlNum(mxio.IoctlKindDefault, ioctlFamilyETH, ioctlOpTXListenStart)
	_, err := m.Ioctl(num, nil, nil)
	if err != nil {
		return fmt.Errorf("IOCTL_ETHERNET_TX_LISTEN_START: %v", err)
	}
	return nil
}
