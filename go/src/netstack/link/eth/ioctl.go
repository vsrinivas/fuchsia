// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package eth

import (
	"encoding/binary"
	"fmt"
	"syscall/zx"
	"syscall/zx/fdio"
)

func IoctlGetTopoPath(m fdio.FDIO) (string, error) {
	res := make([]byte, 1024)
	if _, err := m.Ioctl(fdio.IoctlDeviceGetTopoPath, nil, res); err != nil {
		return "", fmt.Errorf("IOCTL_DEVICE_GET_TOPO_PATH: %v", err)
	}
	// If a device manages per-instance state, the path will begin with an '@' to
	// signify that opening the path will not give the same instance. This is not
	// relevant for netstack, so drop the leading '@' if it exists.
	// See https://fuchsia.googlesource.com/zircon/+/master/docs/ddk/device-ops.md#open
	// for more information on opening devices.
	start := 0
	if res[0] == '@' {
		start = 1
	}
	return string(res[start:]), nil
}

type EthInfo struct {
	Features uint32
	MTU      uint32
	MAC      [6]byte
	_        [2]byte
	_        [12]uint32
} // eth_info_t

// LINT.IfChange
const (
	FeatureWlan     = 0x01
	FeatureSynth    = 0x02
	FeatureLoopback = 0x04
)

// LINT.ThenChange(
//	 //zircon/system/public/zircon/device/ethernet.h,
//	 //garnet/public/fidl/fuchsia.netstack/netstack.fidl
// )

type ethfifos struct {
	// fifo handles
	tx zx.Handle
	rx zx.Handle
	// maximum number of items in fifos
	txDepth uint32
	rxDepth uint32
} // eth_fifos_t

const ioctlFamilyETH = 0x20 // IOCTL_FAMILY_ETH
const (
	ioctlOpGetInfo       = 0 // IOCTL_ETHERNET_GET_INFO,        IOCTL_KIND_DEFAULT
	ioctlOpGetFifos      = 1 // IOCTL_ETHERNET_GET_FIFOS,       IOCTL_KIND_GET_TWO_HANDLES
	ioctlOpSetIobuf      = 2 // IOCTL_ETHERNET_SET_IOBUF,       IOCTL_KIND_SET_HANDLE
	ioctlOpStart         = 3 // IOCTL_ETHERNET_START,           IOCTL_KIND_DEFAULT
	ioctlOpStop          = 4 // IOCTL_ETHERNET_STOP,            IOCTL_KIND_DEFAULT
	ioctlOpTXListenStart = 5 // IOCTL_ETHERNET_TX_LISTEN_START, IOCTL_KIND_DEFAULT
	ioctlOpTXListenStop  = 6 // IOCTL_ETHERNET_TX_LISTEN_STOP,  IOCTL_KIND_DEFAULT
	ioctlOpSetClientName = 7 // IOCTL_ETHERNET_SET_CLIENT_NAME, IOCTL_KIND_DEFAULT
	ioctlOpGetStatus     = 8 // IOCTL_ETHERNET_GET_STATUS,      IOCTL_KIND_DEFAULT
	ioctlOpSetPromisc    = 9 // IOCTL_ETHERNET_SET_PROMISC,     IOCTL_KIND_DEFAULT
)

func IoctlGetInfo(m fdio.FDIO) (info EthInfo, err error) {
	num := fdio.IoctlNum(fdio.IoctlKindDefault, ioctlFamilyETH, ioctlOpGetInfo)
	res := make([]byte, 64)
	if _, err := m.Ioctl(num, nil, res); err != nil {
		return info, fmt.Errorf("IOCTL_ETHERNET_GET_INFO: %v", err)
	}
	info.Features = binary.LittleEndian.Uint32(res)
	info.MTU = binary.LittleEndian.Uint32(res[4:])
	copy(info.MAC[:], res[8:])
	return info, nil
}

func IoctlGetFifos(m fdio.FDIO) (fifos ethfifos, err error) {
	num := fdio.IoctlNum(fdio.IoctlKindGetTwoHandles, ioctlFamilyETH, ioctlOpGetFifos)
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

func IoctlSetIobuf(m fdio.FDIO, h zx.Handle) error {
	num := fdio.IoctlNum(fdio.IoctlKindSetHandle, ioctlFamilyETH, ioctlOpSetIobuf)
	err := m.IoctlSetHandle(num, h)
	if err != nil {
		return fmt.Errorf("IOCTL_ETHERNET_SET_IOBUF: %v", err)
	}
	return nil
}

func IoctlSetClientName(m fdio.FDIO, name []byte) error {
	num := fdio.IoctlNum(fdio.IoctlKindDefault, ioctlFamilyETH, ioctlOpSetClientName)
	_, err := m.Ioctl(num, name, nil)
	if err != nil {
		return fmt.Errorf("IOCTL_ETHERNET_SET_CLIENT_NAME: %v", err)
	}
	return nil
}

func IoctlStart(m fdio.FDIO) error {
	num := fdio.IoctlNum(fdio.IoctlKindDefault, ioctlFamilyETH, ioctlOpStart)
	_, err := m.Ioctl(num, nil, nil)
	if err != nil {
		return fmt.Errorf("IOCTL_ETHERNET_START: %v", err)
	}
	return nil
}

func IoctlStop(m fdio.FDIO) error {
	num := fdio.IoctlNum(fdio.IoctlKindDefault, ioctlFamilyETH, ioctlOpStop)
	_, err := m.Ioctl(num, nil, nil)
	if err != nil {
		return fmt.Errorf("IOCTL_ETHERNET_STOP: %v", err)
	}
	return nil
}

func IoctlTXListenStart(m fdio.FDIO) error {
	num := fdio.IoctlNum(fdio.IoctlKindDefault, ioctlFamilyETH, ioctlOpTXListenStart)
	_, err := m.Ioctl(num, nil, nil)
	if err != nil {
		return fmt.Errorf("IOCTL_ETHERNET_TX_LISTEN_START: %v", err)
	}
	return nil
}

func IoctlGetStatus(m fdio.FDIO) (status uint32, err error) {
	num := fdio.IoctlNum(fdio.IoctlKindDefault, ioctlFamilyETH, ioctlOpGetStatus)
	res := make([]byte, 4)
	_, err = m.Ioctl(num, nil, res)
	if err != nil {
		return 0, fmt.Errorf("IOCTL_ETHERNET_GET_STATUS: %v", err)
	}
	if len(res) != 4 {
		return 0, fmt.Errorf("IOCTL_ETHERNET_GET_STATUS: bad length: %d", len(res))
	}
	status = binary.LittleEndian.Uint32(res)
	return status, nil
}

func IoctlSetPromisc(m fdio.FDIO, enabled bool) error {
	num := fdio.IoctlNum(fdio.IoctlKindDefault, ioctlFamilyETH, ioctlOpSetPromisc)
	in := make([]byte, 1) // sizeof(bool); see zircon/system/public/zircon/device/ioctl-wrapper.h
	if enabled {
		in[0] = 1
	}
	_, err := m.Ioctl(num, in, nil)

	if err != nil {
		return fmt.Errorf("IOCTL_ETHERNET_SET_PROMISC: %v", err)
	}
	return nil
}
