// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file abstracts away code for clients to interact with Amber, to avoid
// duplication, such as error handling, which has some subtleties to it.

package ipcclient

import (
	"fmt"
	"time"

	"fidl/fuchsia/amber"

	"syscall/zx"
	"syscall/zx/zxwait"
)

const ZXSIO_DAEMON_ERROR = zx.SignalUser0

type ErrDaemon string

func NewErrDaemon(str string) ErrDaemon {
	return ErrDaemon(fmt.Sprintf("amber_ctl: daemon error: %s", str))
}

func (e ErrDaemon) Error() string {
	return string(e)
}

func GetUpdateComplete(proxy *amber.ControlInterface, pkgName string, merkle *string) error {
	c, err := proxy.GetUpdateComplete(pkgName, nil, merkle)
	if err == nil {
		defer c.Close()
		b := make([]byte, 64*1024)
		daemonErr := false
		for {
			var err error
			var sigs zx.Signals
			sigs, err = zxwait.Wait(*c.Handle(),
				zx.SignalChannelPeerClosed|zx.SignalChannelReadable|ZXSIO_DAEMON_ERROR,
				zx.Sys_deadline_after(zx.Duration((3 * time.Second).Nanoseconds())))

			// If the daemon signaled an error, wait for the error message to
			// become available. daemonErr could be true if the daemon signaled
			// but the read timed out.
			daemonErr = daemonErr ||
				err == nil && sigs&ZXSIO_DAEMON_ERROR == ZXSIO_DAEMON_ERROR
			if daemonErr {
				sigs, err = zxwait.Wait(*c.Handle(),
					zx.SignalChannelPeerClosed|zx.SignalChannelReadable,
					zx.Sys_deadline_after(zx.Duration((3 * time.Second).Nanoseconds())))
			}

			if sigs&zx.SignalChannelReadable == zx.SignalChannelReadable {
				bs, _, err := c.Read(b, []zx.Handle{}, 0)
				if err != nil {
					return NewErrDaemon(
						fmt.Sprintf("error reading response from channel: %s", err))
				} else if daemonErr {
					return NewErrDaemon(string(b[0:bs]))
				} else {
					fmt.Printf("Wrote update to blob %s\n", string(b[0:bs]))
					return nil
				}
			}

			if sigs&zx.SignalChannelPeerClosed == zx.SignalChannelPeerClosed {
				return NewErrDaemon("response channel closed unexpectedly.")
			} else if err != nil && err.(zx.Error).Status != zx.ErrTimedOut {
				return NewErrDaemon(
					fmt.Sprintf("unknown error while waiting for response from channel: %s", err))
			} else if err != nil && err.(zx.Error).Status == zx.ErrTimedOut {
				fmt.Println("Awaiting response...")
			}
		}
	} else {
		return NewErrDaemon(fmt.Sprintf("error making FIDL request: %s", err))
	}
}
