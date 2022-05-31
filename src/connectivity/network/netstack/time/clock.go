// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package time

import (
	"time"

	"gvisor.dev/gvisor/pkg/tcpip"
)

var _ tcpip.Clock = (*Clock)(nil)

// Clock is a Fuchsia based tcpip.Clock implementation.
type Clock struct{}

// Now implements tcpip.Clock.Now.
func (*Clock) Now() time.Time {
	return time.Now()
}

// NowMonotonic implements tcpip.Clock.NowMonotonic.
func (*Clock) NowMonotonic() tcpip.MonotonicTime {
	var mt tcpip.MonotonicTime
	return mt.Add(time.Duration(Now().MonotonicNano()))
}

// AfterFunc implements tcpip.Clock.AfterFunc.
func (*Clock) AfterFunc(d time.Duration, f func()) tcpip.Timer {
	return &timer{
		stdTimer: time.AfterFunc(d, f),
	}
}

type timer struct {
	stdTimer *time.Timer
}

var _ tcpip.Timer = (*timer)(nil)

// Stop implements tcpip.Timer.Stop.
func (t *timer) Stop() bool {
	return t.stdTimer.Stop()
}

// Reset implements tcpip.Timer.Reset.
func (t *timer) Reset(d time.Duration) {
	t.stdTimer.Reset(d)
}
