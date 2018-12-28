// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"encoding/binary"
	"fmt"
	"time"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/header"
)

// #cgo CFLAGS: -D_GNU_SOURCE
// #cgo CFLAGS: -I${SRCDIR}/../../../../zircon/system/ulib/zxs/include
// #cgo CFLAGS: -I${SRCDIR}/../../../../zircon/third_party/ulib/musl/include/
// #cgo CFLAGS: -I${SRCDIR}/../../../public
// #include <lib/zxs/protocol.h>
// #include <netinet/tcp.h>
// #include <lib/netstack/c/netconfig.h>
import "C"

func isZeros(b []byte) bool {
	for _, b := range b {
		if b != 0 {
			return false
		}
	}
	return true
}

func (v *C.struct_sockaddr_in) Decode() tcpip.FullAddress {
	out := tcpip.FullAddress{
		Port: binary.BigEndian.Uint16(v.sin_port.Bytes()),
	}
	if b := v.sin_addr.Bytes(); !isZeros(b) {
		out.Addr = tcpip.Address(b)
	}
	return out
}

func (v *C.struct_sockaddr_in) Encode(addr tcpip.FullAddress) error {
	v.sin_family = C.AF_INET
	if n := copy(v.sin_addr.Bytes(), addr.Addr); n < header.IPv4AddressSize {
		return fmt.Errorf("short %T: %d/%d", v, n, header.IPv4AddressSize)
	}
	binary.BigEndian.PutUint16(v.sin_port.Bytes(), addr.Port)
	return nil
}

func (v *C.struct_sockaddr_in6) Decode() tcpip.FullAddress {
	out := tcpip.FullAddress{
		Port: binary.BigEndian.Uint16(v.sin6_port.Bytes()),
	}
	if b := v.sin6_addr.Bytes(); !isZeros(b) {
		out.Addr = tcpip.Address(b)
	}
	return out
}

func (v *C.struct_sockaddr_in6) Encode(addr tcpip.FullAddress) error {
	v.sin6_family = C.AF_INET6
	if n := copy(v.sin6_addr.Bytes(), addr.Addr); n < header.IPv6AddressSize {
		return fmt.Errorf("short %T: %d/%d", v, n, header.IPv6AddressSize)
	}
	binary.BigEndian.PutUint16(v.sin6_port.Bytes(), addr.Port)
	return nil
}

func (v *C.struct_zxrio_sockopt_req_reply) ToInt() int {
	var result int
	for i, b := range v.opt()[:v.optlen] {
		result += int(b) << (uint(i) * 8)
	}
	return result
}

// TODO: create a tcpip.Option type
func (v *C.struct_zxrio_sockopt_req_reply) Unpack() (interface{}, error) {
	switch v.level {
	case C.SOL_SOCKET:
		switch v.optname {
		case C.SO_ERROR:
			return tcpip.ErrorOption{}, nil
		case C.SO_REUSEADDR:
			return tcpip.ReuseAddressOption(v.ToInt()), nil
		case C.SO_REUSEPORT:
			return tcpip.ReusePortOption(v.ToInt()), nil
		case C.SO_KEEPALIVE:
			return tcpip.KeepaliveEnabledOption(v.ToInt()), nil
		case C.SO_BROADCAST:
		case C.SO_DEBUG:
		case C.SO_SNDBUF:
		case C.SO_RCVBUF:
		}
	case C.SOL_IP:
		switch v.optname {
		case C.IP_TOS:
		case C.IP_TTL:
		case C.IP_MULTICAST_IF:
		case C.IP_MULTICAST_TTL:
			return tcpip.MulticastTTLOption(v.ToInt()), nil
		case C.IP_MULTICAST_LOOP:
		case C.IP_ADD_MEMBERSHIP, C.IP_DROP_MEMBERSHIP:
			var o tcpip.MembershipOption

			b := v.opt()[:v.optlen]
			switch len(b) {
			case C.sizeof_struct_ip_mreq:
				var mreq C.struct_ip_mreq
				if err := mreq.Unmarshal(b); err != nil {
					return nil, err
				}

				o = tcpip.MembershipOption{
					MulticastAddr: tcpip.Address(mreq.imr_multiaddr.Bytes()),
					InterfaceAddr: tcpip.Address(mreq.imr_interface.Bytes()),
				}
			case C.sizeof_struct_ip_mreqn:
				var mreqn C.struct_ip_mreqn
				if err := mreqn.Unmarshal(b); err != nil {
					return nil, err
				}

				o = tcpip.MembershipOption{
					NIC:           tcpip.NICID(mreqn.imr_ifindex),
					MulticastAddr: tcpip.Address(mreqn.imr_multiaddr.Bytes()),
					InterfaceAddr: tcpip.Address(mreqn.imr_address.Bytes()),
				}
			default:
				return nil, fmt.Errorf("sockopt: bad argument %+v", v)
			}
			switch v.optname {
			case C.IP_ADD_MEMBERSHIP:
				return tcpip.AddMembershipOption(o), nil
			case C.IP_DROP_MEMBERSHIP:
				return tcpip.RemoveMembershipOption(o), nil
			default:
				panic("unreachable")
			}
		}
	case C.SOL_TCP:
		switch v.optname {
		case C.TCP_NODELAY:
			var delay int
			noDelay := v.ToInt()
			if noDelay != 0 {
				delay = 0
			} else {
				delay = 1
			}
			return tcpip.DelayOption(delay), nil
		case C.TCP_INFO:
			return tcpip.TCPInfoOption{}, nil
		case C.TCP_MAXSEG:
		case C.TCP_CORK:
		case C.TCP_KEEPIDLE:
			return tcpip.KeepaliveIdleOption(time.Duration(v.ToInt()) * time.Second), nil
		case C.TCP_KEEPINTVL:
			return tcpip.KeepaliveIntervalOption(time.Duration(v.ToInt()) * time.Second), nil
		case C.TCP_KEEPCNT:
			return tcpip.KeepaliveCountOption(v.ToInt()), nil
		case C.TCP_SYNCNT:
		case C.TCP_LINGER2:
		case C.TCP_DEFER_ACCEPT:
		case C.TCP_WINDOW_CLAMP:
		case C.TCP_QUICKACK:
		}
	}
	return nil, nil
}
