// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package arduinorelay

import (
	"fmt"
	"io"
	"strconv"
	"time"

	"fuchsia.googlesource.com/tools/serial"
)

const baudRate = 115200

func Reboot(path string, port string) error {
	// Port will be an integer here.
	portNum, err := strconv.Atoi(port)
	if err != nil {
		return err
	}
	// Port number must not be 0 since that means "reboot all" and valid ports
	// are only hexadecimal digits 1-F.
	if portNum <= 0 || portNum >= 15 {
		return fmt.Errorf("port number must be >0 and <16, got %d", portNum)
	}

	// Open the device for writing with 10 second timeout.
	device, err := serial.Open(path, baudRate, 10)
	if err != nil {
		return err
	}

	// Turn off device.
	n, err := io.WriteString(device, fmt.Sprintf("%X0", portNum))
	if err != nil {
		return err
	} else if n != 2 {
		return fmt.Errorf("failed to write all command bytes")
	}

	// Make sure the capacitors on the boards actually go
	// low before turning back on, otherwise we get "zombie" boards.
	time.Sleep(1 * time.Second)

	// Turn the device back on.
	n, err = io.WriteString(device, fmt.Sprintf("%X1", portNum))
	if err != nil {
		return err
	} else if n != 2 {
		return fmt.Errorf("failed to write all command bytes")
	}
	return nil
}
