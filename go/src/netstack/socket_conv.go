// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"log"
	"time"

	"github.com/google/netstack/tcpip"
)

// #cgo CFLAGS: -D_GNU_SOURCE
// #cgo CFLAGS: -I${SRCDIR}/../../../../zircon/system/ulib/zxs/include
// #cgo CFLAGS: -I${SRCDIR}/../../../../zircon/third_party/ulib/musl/include/
// #cgo CFLAGS: -I${SRCDIR}/../../../public
// #include <lib/zxs/protocol.h>
// #include <netinet/tcp.h>
// #include <lib/netstack/c/netconfig.h>
import "C"

func isZeros(buf []byte) bool {
	for i := 0; i < len(buf); i++ {
		if buf[i] != 0 {
			return false
		}
	}
	return true
}

func (v *C.struct_zxrio_sockopt_req_reply) intValue() int {
	var result int
	for i, b := range v.optBytes() {
		result += int(b) << (uint(i) * 8)
	}
	return result
}

// TODO: create a tcpip.Option type
func (v *C.struct_zxrio_sockopt_req_reply) Unpack() interface{} {
	switch v.level {
	case C.SOL_SOCKET:
		switch v.optname {
		case C.SO_ERROR:
			return tcpip.ErrorOption{}
		case C.SO_REUSEADDR:
			return tcpip.ReuseAddressOption(v.intValue())
		case C.SO_KEEPALIVE:
			return tcpip.KeepaliveEnabledOption(v.intValue())
		case C.SO_BROADCAST:
		case C.SO_DEBUG:
		case C.SO_SNDBUF:
		case C.SO_RCVBUF:
		}
		log.Printf("convSockOpt: TODO SOL_SOCKET optname=%d", v.optname)
	case C.SOL_IP:
		switch v.optname {
		case C.IP_TOS:
		case C.IP_TTL:
		case C.IP_MULTICAST_IF:
		case C.IP_MULTICAST_TTL:
			if len(v.optval) < 1 {
				log.Printf("sockopt: bad argument to IP_MULTICAST_TTL")
				return nil
			}
			return tcpip.MulticastTTLOption(v.optval[0])
		case C.IP_MULTICAST_LOOP:
		case C.IP_ADD_MEMBERSHIP, C.IP_DROP_MEMBERSHIP:
			mreqn := C.struct_ip_mreqn{}
			if err := mreqn.Decode(v.optBytes()); err != nil {
				// If we fail to decode a C.struct_ip_mreqn, try to decode a C.struct_ip_mreq.
				mreq := C.struct_ip_mreq{}
				if err := mreq.Decode(v.optBytes()); err != nil {
					log.Printf("sockopt: bad argument to %d", v.optname)
					return nil
				}
				mreqn.imr_multiaddr = mreq.imr_multiaddr
				mreqn.imr_address = mreq.imr_interface
				mreqn.imr_ifindex = 0
			}
			option := tcpip.MembershipOption{
				NIC:           tcpip.NICID(mreqn.imr_ifindex),
				InterfaceAddr: tcpip.Address(mreqn.imr_address.Bytes()),
				MulticastAddr: tcpip.Address(mreqn.imr_multiaddr.Bytes()),
			}
			if v.optname == C.IP_ADD_MEMBERSHIP {
				return tcpip.AddMembershipOption(option)
			}
			return tcpip.RemoveMembershipOption(option)
		}
		log.Printf("convSockOpt: TODO IPPROTO_IP optname=%d", v.optname)
	case C.SOL_TCP:
		switch v.optname {
		case C.TCP_NODELAY:
			var delay int
			noDelay := v.intValue()
			if noDelay != 0 {
				delay = 0
			} else {
				delay = 1
			}
			return tcpip.DelayOption(delay)
		case C.TCP_INFO:
			return tcpip.TCPInfoOption{}
		case C.TCP_MAXSEG:
		case C.TCP_CORK:
		case C.TCP_KEEPIDLE:
			return tcpip.KeepaliveIdleOption(time.Duration(v.intValue()) * time.Second)
		case C.TCP_KEEPINTVL:
			return tcpip.KeepaliveIntervalOption(time.Duration(v.intValue()) * time.Second)
		case C.TCP_KEEPCNT:
			return tcpip.KeepaliveCountOption(v.intValue())
		case C.TCP_SYNCNT:
		case C.TCP_LINGER2:
		case C.TCP_DEFER_ACCEPT:
		case C.TCP_WINDOW_CLAMP:
		case C.TCP_QUICKACK:
		}
		log.Printf("convSockOpt: TODO SOL_TCP optname=%d", v.optname)
	}
	return nil
}
