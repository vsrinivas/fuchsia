// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package serial

import (
	"fmt"
	"io"
	"os"
	"syscall"

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

// cfmakeraw makes a termios configuration that is similar to the configuration
// produced by glibc's cfmakeraw, which is to turn off all input and output
// processing, and configure in non-canonical mode - aka "just give me the data
// stream"
func cfmakeraw(t *unix.Termios) {
	t.Iflag &^= unix.IGNBRK | unix.BRKINT | unix.PARMRK | unix.ISTRIP | unix.INLCR | unix.IGNCR | unix.ICRNL | unix.IXON
	t.Oflag &^= unix.OPOST
	t.Lflag &^= unix.ECHO | unix.ECHONL | unix.ICANON | unix.ISIG | unix.IEXTEN
	t.Cflag &^= unix.CSIZE | unix.PARENB
	t.Cflag |= unix.CS8
}

func open(name string, baudRate int) (io.ReadWriteCloser, error) {
	rate, ok := supportedBaudRates[baudRate]
	if !ok {
		return nil, fmt.Errorf("unsupported baud rate: %d", baudRate)
	}
	// open non-blocking to avoid waiting for carrier detect
	f, err := os.OpenFile(name, unix.O_RDWR|unix.O_NOCTTY|unix.O_NONBLOCK, 0666)
	if err != nil {
		return nil, err
	}
	// we actually want blocking semantics downstream, so unset that. note: use
	// syscall, not unix, because we're going to call Read/Write and may need the
	// runtime poller to know we've done this.
	if err := syscall.SetNonblock(int(f.Fd()), false); err != nil {
		f.Close()
		return nil, err
	}

	t := unix.Termios{}

	// start with a raw mode configuration, that is non-canonical mode, post
	// processing off, no line end processing, etc.
	cfmakeraw(&t)

	// ignore framing and parity errors
	t.Iflag |= unix.IGNPAR

	// enable receiver, and ignore modem control lines
	t.Cflag |= unix.CREAD | unix.CLOCAL

	// one stop bit
	t.Cflag &^= unix.CSTOPB

	// disable hardware flow control (currently generally not connected)
	t.Cflag &^= unix.CRTSCTS

	// do not delay reads and do not return 0 reads
	t.Cc[unix.VTIME] = 0
	// return 1 or more characters as soon as they are ready
	t.Cc[unix.VMIN] = 1

	// set baud rate
	t.Ispeed = rate
	t.Ospeed = rate

	if err := unix.IoctlSetTermios(int(f.Fd()), unix.TCSETA, &t); err != nil {
		f.Close()
		return nil, err
	}

	return f, nil
}
