// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package serial

import (
	"fmt"
	"io"
	"os"
	"unsafe"

	"golang.org/x/sys/unix"
)

var supportedBaudRates = map[int]uint32{
	50:      unix.B50,
	75:      unix.B75,
	110:     unix.B110,
	134:     unix.B134,
	150:     unix.B150,
	200:     unix.B200,
	300:     unix.B300,
	600:     unix.B600,
	1200:    unix.B1200,
	1800:    unix.B1800,
	2400:    unix.B2400,
	4800:    unix.B4800,
	9600:    unix.B9600,
	19200:   unix.B19200,
	38400:   unix.B38400,
	57600:   unix.B57600,
	115200:  unix.B115200,
	230400:  unix.B230400,
	460800:  unix.B460800,
	500000:  unix.B500000,
	576000:  unix.B576000,
	921600:  unix.B921600,
	1000000: unix.B1000000,
	1152000: unix.B1152000,
	1500000: unix.B1500000,
	2000000: unix.B2000000,
	2500000: unix.B2500000,
	3000000: unix.B3000000,
	3500000: unix.B3500000,
	4000000: unix.B4000000,
}

func open(name string, baudRate int, timeoutSecs int) (io.ReadWriteCloser, error) {
	rate, ok := supportedBaudRates[baudRate]
	if !ok {
		return nil, fmt.Errorf("unsupported baud rate: %d", baudRate)
	}
	if timeoutSecs < 0 || (timeoutSecs*10) > (1<<8) {
		return nil, fmt.Errorf("timeout must be between 0 and 25, got: %d", timeoutSecs)
	}
	f, err := os.OpenFile(name, unix.O_RDWR|unix.O_NOCTTY|unix.O_NONBLOCK, 0666)
	if err != nil {
		return nil, err
	}
	t := unix.Termios{
		Iflag:  unix.IGNPAR,
		Cflag:  unix.CREAD | unix.CLOCAL | unix.CS8 | rate,
		Ispeed: rate,
		Ospeed: rate,
	}
	t.Cc[unix.VTIME] = uint8(timeoutSecs * 10)
	_, _, errno := unix.Syscall6(
		unix.SYS_IOCTL,
		uintptr(f.Fd()),
		uintptr(unix.TCSETS),
		uintptr(unsafe.Pointer(&t)),
		0,
		0,
		0,
	)
	if errno != 0 {
		return nil, errno
	}
	if err := unix.SetNonblock(int(f.Fd()), false); err != nil {
		return nil, err
	}
	return f, nil
}
