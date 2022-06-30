// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

var _ stack.MulticastForwardingEventDispatcher = (*multicastEventDispatcher)(nil)

// TODO(https://fxbug.dev/102559): Implement an event handler. In the meantime,
// an empty implementation is required to enable multicast forwarding.
type multicastEventDispatcher struct{}

func (m *multicastEventDispatcher) OnMissingRoute(context stack.MulticastPacketContext) {
	syslog.Warnf("OnMissingRoute(%#v) called with unimplemented handler.", context)
}

func (m *multicastEventDispatcher) OnUnexpectedInputInterface(context stack.MulticastPacketContext, expectedInputInterface tcpip.NICID) {
	syslog.Warnf("OnUnexpectedInputInterface(%#v, %d) called with unimplemented handler.", context, expectedInputInterface)
}
