// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"encoding/binary"
	"math"
	"net"
	"time"

	"syslog/logger"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/network/ipv4"
	"github.com/google/netstack/tcpip/network/ipv6"
	"github.com/google/netstack/tcpip/transport/tcp"
	"github.com/google/netstack/tcpip/transport/udp"
)

// #cgo CFLAGS: -D_GNU_SOURCE
// #cgo CFLAGS: -I${SRCDIR}/../../../../zircon/third_party/ulib/musl/include/
// #include <netinet/in.h>
// #include <netinet/tcp.h>
// #include <netinet/udp.h>
import "C"

// Functions below are adapted from
// github.com/google/gvisor/pkg/sentry/socket/epsocket/epsocket.go.
//
// At the time of writing, this command produces a reasonable diff:
//
// curl -sfSL https://raw.githubusercontent.com/google/gvisor/master/pkg/sentry/socket/epsocket/epsocket.go |
//   sed s/linux/C/g | \
//   sed 's/, outLen)/)/g' | \
//   sed 's/(t, /(/g' | \
//   sed 's/(s, /(/g' | \
//   sed 's/, family,/,/g' | \
//   sed 's/, skType,/, transProto,/g' | \
//   diff --color --ignore-all-space --unified - go/src/netstack/socket_conv.go

const sizeOfInt32 int = 4

func GetSockOpt(ep tcpip.Endpoint, netProto tcpip.NetworkProtocolNumber, transProto tcpip.TransportProtocolNumber, level, name int16) (interface{}, *tcpip.Error) {
	switch level {
	case C.SOL_SOCKET:
		return getSockOptSocket(ep, netProto, transProto, name)

	case C.SOL_TCP:
		return getSockOptTCP(ep, name)

	case C.SOL_IPV6:
		return getSockOptIPv6(ep, name)

	case C.SOL_IP:
		return getSockOptIP(ep, name)

	case
		C.SOL_UDP,
		C.SOL_ICMPV6,
		C.SOL_RAW,
		C.SOL_PACKET:

	default:
		logger.Infof("unimplemented getsockopt: level=%d name=%d", level, name)

	}
	return nil, tcpip.ErrUnknownProtocol
}

func getSockOptSocket(ep tcpip.Endpoint, netProto tcpip.NetworkProtocolNumber, transProto tcpip.TransportProtocolNumber, name int16) (interface{}, *tcpip.Error) {
	switch name {
	case C.SO_TYPE:
		switch transProto {
		case tcp.ProtocolNumber:
			return int32(C.SOCK_STREAM), nil

		case udp.ProtocolNumber:
			return int32(C.SOCK_DGRAM), nil

		default:
			return 0, tcpip.ErrNotSupported
		}

	case C.SO_DOMAIN:
		switch netProto {
		case ipv4.ProtocolNumber:
			return int32(C.AF_INET), nil

		case ipv6.ProtocolNumber:
			return int32(C.AF_INET6), nil

		default:
			return 0, tcpip.ErrNotSupported
		}

	case C.SO_ERROR:
		// Get the last error and convert it.
		err := ep.GetSockOpt(tcpip.ErrorOption{})
		if err == nil {
			return int32(0), nil
		}
		return int32(tcpipErrorToCode(err)), nil

	case C.SO_PEERCRED:
		return nil, tcpip.ErrNotSupported

	case C.SO_PASSCRED:
		var v tcpip.PasscredOption
		if err := ep.GetSockOpt(&v); err != nil {
			return nil, err
		}

		return int32(v), nil

	case C.SO_SNDBUF:
		var size tcpip.SendBufferSizeOption
		if err := ep.GetSockOpt(&size); err != nil {
			return nil, err
		}

		if size > math.MaxInt32 {
			size = math.MaxInt32
		}

		return int32(size), nil

	case C.SO_RCVBUF:
		var size tcpip.ReceiveBufferSizeOption
		if err := ep.GetSockOpt(&size); err != nil {
			return nil, err
		}

		if size > math.MaxInt32 {
			size = math.MaxInt32
		}

		return int32(size), nil

	case C.SO_REUSEADDR:
		var v tcpip.ReuseAddressOption
		if err := ep.GetSockOpt(&v); err != nil {
			return nil, err
		}

		return int32(v), nil

	case C.SO_REUSEPORT:
		var v tcpip.ReusePortOption
		if err := ep.GetSockOpt(&v); err != nil {
			return nil, err
		}

		return int32(v), nil

	case C.SO_BROADCAST:
		var v tcpip.BroadcastOption
		if err := ep.GetSockOpt(&v); err != nil {
			return nil, err
		}

		return int32(v), nil

	case C.SO_KEEPALIVE:
		var v tcpip.KeepaliveEnabledOption
		if err := ep.GetSockOpt(&v); err != nil {
			return nil, err
		}

		return int32(v), nil

	case C.SO_LINGER:
		return C.struct_linger{}, nil

	case C.SO_SNDTIMEO:
		return nil, tcpip.ErrNotSupported

	case C.SO_RCVTIMEO:
		return nil, tcpip.ErrNotSupported

	case C.SO_OOBINLINE:
		var v tcpip.OutOfBandInlineOption
		if err := ep.GetSockOpt(&v); err != nil {
			return nil, err
		}

		return int32(v), nil

	default:
		logger.Infof("unimplemented getsockopt: SOL_SOCKET name=%d", name)

	}
	return nil, tcpip.ErrUnknownProtocolOption
}

func getSockOptTCP(ep tcpip.Endpoint, name int16) (interface{}, *tcpip.Error) {
	switch name {
	case C.TCP_NODELAY:
		var v tcpip.DelayOption
		if err := ep.GetSockOpt(&v); err != nil {
			return nil, err
		}

		if v == 0 {
			return int32(1), nil
		}
		return int32(0), nil

	case C.TCP_CORK:
		var v tcpip.CorkOption
		if err := ep.GetSockOpt(&v); err != nil {
			return nil, err
		}

		return int32(v), nil

	case C.TCP_QUICKACK:
		var v tcpip.QuickAckOption
		if err := ep.GetSockOpt(&v); err != nil {
			return nil, err
		}

		return int32(v), nil

	case C.TCP_KEEPIDLE:
		var v tcpip.KeepaliveIdleOption
		if err := ep.GetSockOpt(&v); err != nil {
			return nil, err
		}

		return int32(time.Duration(v) / time.Second), nil

	case C.TCP_KEEPINTVL:
		var v tcpip.KeepaliveIntervalOption
		if err := ep.GetSockOpt(&v); err != nil {
			return nil, err
		}

		return int32(time.Duration(v) / time.Second), nil

	case C.TCP_INFO:
		var v tcpip.TCPInfoOption
		if err := ep.GetSockOpt(&v); err != nil {
			return nil, err
		}

		return C.struct_tcp_info{
			// Microseconds.
			tcpi_rtt:    C.uint(v.RTT.Nanoseconds() / 1000),
			tcpi_rttvar: C.uint(v.RTTVar.Nanoseconds() / 1000),
		}, nil

	case
		C.TCP_CC_INFO,
		C.TCP_NOTSENT_LOWAT:

	default:
		logger.Infof("unimplemented getsockopt: SOL_TCP name=%d", name)

	}
	return nil, tcpip.ErrUnknownProtocolOption
}

func getSockOptIPv6(ep tcpip.Endpoint, name int16) (interface{}, *tcpip.Error) {
	switch name {
	case C.IPV6_V6ONLY:
		var v tcpip.V6OnlyOption
		if err := ep.GetSockOpt(&v); err != nil {
			return nil, err
		}

		return int32(v), nil

	case C.IPV6_PATHMTU:

	default:
		logger.Infof("unimplemented getsockopt: SOL_IPV6 name=%d", name)

	}
	return nil, tcpip.ErrUnknownProtocolOption
}

func getSockOptIP(ep tcpip.Endpoint, name int16) (interface{}, *tcpip.Error) {
	switch name {
	case C.IP_MULTICAST_TTL:
		var v tcpip.MulticastTTLOption
		if err := ep.GetSockOpt(&v); err != nil {
			return nil, err
		}

		return int32(v), nil

	case C.IP_MULTICAST_IF:
		var v tcpip.MulticastInterfaceOption
		if err := ep.GetSockOpt(&v); err != nil {
			return nil, err
		}

		if len(v.InterfaceAddr) == 0 {
			return []byte(net.IPv4zero.To4()), nil
		}

		return []byte((v.InterfaceAddr)), nil

	case C.IP_MULTICAST_LOOP:
		var v tcpip.MulticastLoopOption
		if err := ep.GetSockOpt(&v); err != nil {
			return nil, err
		}

		if v {
			return int32(1), nil
		}

		return int32(0), nil

	default:
		logger.Infof("unimplemented getsockopt: SOL_IP name=%d", name)

	}
	return nil, tcpip.ErrUnknownProtocolOption
}

func SetSockOpt(ep tcpip.Endpoint, level, name int16, optVal []uint8) *tcpip.Error {
	switch level {
	case C.SOL_SOCKET:
		return setSockOptSocket(ep, name, optVal)

	case C.SOL_TCP:
		return setSockOptTCP(ep, name, optVal)

	case C.SOL_IPV6:
		return setSockOptIPv6(ep, name, optVal)

	case C.SOL_IP:
		return setSockOptIP(ep, name, optVal)

	case C.SOL_UDP,
		C.SOL_ICMPV6,
		C.SOL_RAW,
		C.SOL_PACKET:

	default:
		logger.Infof("unimplemented setsockopt: level=%d name=%d optVal=%x", level, name, optVal)

	}
	return tcpip.ErrUnknownProtocolOption
}

func setSockOptSocket(ep tcpip.Endpoint, name int16, optVal []byte) *tcpip.Error {
	switch name {
	case C.SO_SNDBUF:
		if len(optVal) < sizeOfInt32 {
			return tcpip.ErrInvalidOptionValue
		}

		v := binary.LittleEndian.Uint32(optVal)
		return ep.SetSockOpt(tcpip.SendBufferSizeOption(v))

	case C.SO_RCVBUF:
		if len(optVal) < sizeOfInt32 {
			return tcpip.ErrInvalidOptionValue
		}

		v := binary.LittleEndian.Uint32(optVal)
		return ep.SetSockOpt(tcpip.ReceiveBufferSizeOption(v))

	case C.SO_REUSEADDR:
		if len(optVal) < sizeOfInt32 {
			return tcpip.ErrInvalidOptionValue
		}

		v := binary.LittleEndian.Uint32(optVal)
		return ep.SetSockOpt(tcpip.ReuseAddressOption(v))

	case C.SO_REUSEPORT:
		if len(optVal) < sizeOfInt32 {
			return tcpip.ErrInvalidOptionValue
		}

		v := binary.LittleEndian.Uint32(optVal)
		return ep.SetSockOpt(tcpip.ReusePortOption(v))

	case C.SO_BROADCAST:
		if len(optVal) < sizeOfInt32 {
			return tcpip.ErrInvalidOptionValue
		}

		v := binary.LittleEndian.Uint32(optVal)
		return ep.SetSockOpt(tcpip.BroadcastOption(v))

	case C.SO_PASSCRED:
		if len(optVal) < sizeOfInt32 {
			return tcpip.ErrInvalidOptionValue
		}

		v := binary.LittleEndian.Uint32(optVal)
		return ep.SetSockOpt(tcpip.PasscredOption(v))

	case C.SO_KEEPALIVE:
		if len(optVal) < sizeOfInt32 {
			return tcpip.ErrInvalidOptionValue
		}

		v := binary.LittleEndian.Uint32(optVal)
		return ep.SetSockOpt(tcpip.KeepaliveEnabledOption(v))

	case C.SO_SNDTIMEO:
		return tcpip.ErrNotSupported

	case C.SO_RCVTIMEO:
		return tcpip.ErrNotSupported

	default:
		logger.Infof("unimplemented setsockopt: SOL_SOCKET name=%d optVal=%x", name, optVal)

	}
	return tcpip.ErrUnknownProtocolOption
}

// setSockOptTCP implements SetSockOpt when level is SOL_TCP.
func setSockOptTCP(ep tcpip.Endpoint, name int16, optVal []byte) *tcpip.Error {
	switch name {
	case C.TCP_NODELAY:
		if len(optVal) < sizeOfInt32 {
			return tcpip.ErrInvalidOptionValue
		}

		v := binary.LittleEndian.Uint32(optVal)
		var o tcpip.DelayOption
		if v == 0 {
			o = 1
		}
		return ep.SetSockOpt(o)

	case C.TCP_CORK:
		if len(optVal) < sizeOfInt32 {
			return tcpip.ErrInvalidOptionValue
		}

		v := binary.LittleEndian.Uint32(optVal)
		return ep.SetSockOpt(tcpip.CorkOption(v))

	case C.TCP_QUICKACK:
		if len(optVal) < sizeOfInt32 {
			return tcpip.ErrInvalidOptionValue
		}

		v := binary.LittleEndian.Uint32(optVal)
		return ep.SetSockOpt(tcpip.QuickAckOption(v))

	case C.TCP_KEEPIDLE:
		if len(optVal) < sizeOfInt32 {
			return tcpip.ErrInvalidOptionValue
		}

		v := binary.LittleEndian.Uint32(optVal)
		return ep.SetSockOpt(tcpip.KeepaliveIdleOption(time.Second * time.Duration(v)))

	case C.TCP_KEEPINTVL:
		if len(optVal) < sizeOfInt32 {
			return tcpip.ErrInvalidOptionValue
		}

		v := binary.LittleEndian.Uint32(optVal)
		return ep.SetSockOpt(tcpip.KeepaliveIntervalOption(time.Second * time.Duration(v)))

	case C.TCP_REPAIR_OPTIONS:

	default:
		logger.Infof("unimplemented setsockopt: SOL_TCP name=%d optVal=%x", name, optVal)

	}
	return tcpip.ErrUnknownProtocolOption
}

// setSockOptIPv6 implements SetSockOpt when level is SOL_IPV6.
func setSockOptIPv6(ep tcpip.Endpoint, name int16, optVal []byte) *tcpip.Error {
	switch name {
	case C.IPV6_V6ONLY:
		if len(optVal) < sizeOfInt32 {
			return tcpip.ErrInvalidOptionValue
		}

		v := binary.LittleEndian.Uint32(optVal)
		return ep.SetSockOpt(tcpip.V6OnlyOption(v))

	case C.IPV6_ADD_MEMBERSHIP, C.IPV6_DROP_MEMBERSHIP:
		var ipv6_mreq C.struct_ipv6_mreq
		if err := ipv6_mreq.Unmarshal(optVal); err != nil {
			return tcpip.ErrInvalidOptionValue
		}

		o := tcpip.MembershipOption{
			NIC:           tcpip.NICID(ipv6_mreq.ipv6mr_interface),
			MulticastAddr: tcpip.Address(ipv6_mreq.ipv6mr_multiaddr.Bytes()),
		}
		switch name {
		case C.IPV6_ADD_MEMBERSHIP:
			return ep.SetSockOpt(tcpip.AddMembershipOption(o))
		case C.IPV6_DROP_MEMBERSHIP:
			return ep.SetSockOpt(tcpip.RemoveMembershipOption(o))
		default:
			panic("unreachable")
		}

	case
		C.IPV6_IPSEC_POLICY,
		C.IPV6_JOIN_ANYCAST,
		C.IPV6_LEAVE_ANYCAST,
		C.IPV6_PKTINFO,
		C.IPV6_ROUTER_ALERT,
		C.IPV6_XFRM_POLICY,
		C.MCAST_BLOCK_SOURCE,
		C.MCAST_JOIN_GROUP,
		C.MCAST_JOIN_SOURCE_GROUP,
		C.MCAST_LEAVE_GROUP,
		C.MCAST_LEAVE_SOURCE_GROUP,
		C.MCAST_UNBLOCK_SOURCE:

	default:
		logger.Infof("unimplemented setsockopt: SOL_IPV6 name=%d optVal=%x", name, optVal)

	}
	return tcpip.ErrUnknownProtocolOption
}

// setSockOptIP implements SetSockOpt when level is SOL_IP.
func setSockOptIP(ep tcpip.Endpoint, name int16, optVal []byte) *tcpip.Error {
	switch name {
	case C.IP_MULTICAST_TTL:
		if len(optVal) == 0 {
			return tcpip.ErrInvalidOptionValue
		}

		if len(optVal) > sizeOfInt32 {
			optVal = optVal[:sizeOfInt32]
		}
		var v int32
		for i, b := range optVal {
			v += int32(b) << uint(i*8)
		}

		if v == -1 {
			// Linux translates -1 to 1.
			v = 1
		}
		return ep.SetSockOpt(tcpip.MulticastTTLOption(v))

	case C.IP_ADD_MEMBERSHIP, C.IP_DROP_MEMBERSHIP, C.IP_MULTICAST_IF:
		var mreqn C.struct_ip_mreqn

		switch len(optVal) {
		case C.sizeof_struct_ip_mreq:
			var mreq C.struct_ip_mreq
			if err := mreq.Unmarshal(optVal); err != nil {
				return tcpip.ErrInvalidOptionValue
			}

			mreqn.imr_multiaddr = mreq.imr_multiaddr
			mreqn.imr_address = mreq.imr_interface

		case C.sizeof_struct_ip_mreqn:
			if err := mreqn.Unmarshal(optVal); err != nil {
				return tcpip.ErrInvalidOptionValue
			}

		case C.sizeof_struct_in_addr:
			if name == C.IP_MULTICAST_IF {
				copy(mreqn.imr_address.Bytes(), optVal)
				break
			}
			fallthrough

		default:
			return tcpip.ErrInvalidOptionValue

		}

		switch name {
		case C.IP_ADD_MEMBERSHIP, C.IP_DROP_MEMBERSHIP:
			o := tcpip.MembershipOption{
				NIC:           tcpip.NICID(mreqn.imr_ifindex),
				MulticastAddr: tcpip.Address(mreqn.imr_multiaddr.Bytes()),
				InterfaceAddr: tcpip.Address(mreqn.imr_address.Bytes()),
			}

			switch name {
			case C.IP_ADD_MEMBERSHIP:
				return ep.SetSockOpt(tcpip.AddMembershipOption(o))

			case C.IP_DROP_MEMBERSHIP:
				return ep.SetSockOpt(tcpip.RemoveMembershipOption(o))

			default:
				panic("unreachable")

			}
		case C.IP_MULTICAST_IF:
			interfaceAddr := mreqn.imr_address.Bytes()
			if isZeros(interfaceAddr) {
				interfaceAddr = nil
			}

			return ep.SetSockOpt(tcpip.MulticastInterfaceOption{
				NIC:           tcpip.NICID(mreqn.imr_ifindex),
				InterfaceAddr: tcpip.Address(interfaceAddr),
			})

		default:
			panic("unreachable")

		}

	case C.IP_MULTICAST_LOOP:
		if len(optVal) == 0 {
			return tcpip.ErrInvalidOptionValue
		}

		if len(optVal) > sizeOfInt32 {
			optVal = optVal[:sizeOfInt32]
		}
		var v int32
		for i, b := range optVal {
			v += int32(b) << uint(i*8)
		}

		return ep.SetSockOpt(tcpip.MulticastLoopOption(v != 0))

	case C.MCAST_JOIN_GROUP:
		// FIXME: Disallow IP-level multicast group options by
		// default. These will need to be supported by appropriately plumbing
		// the level through to the network stack (if at all). However, we
		// still allow setting TTL, and multicast-enable/disable type options.
		return tcpip.ErrNotSupported

	case
		C.IP_ADD_SOURCE_MEMBERSHIP,
		C.IP_BIND_ADDRESS_NO_PORT,
		C.IP_BLOCK_SOURCE,
		C.IP_CHECKSUM,
		C.IP_DROP_SOURCE_MEMBERSHIP,
		C.IP_FREEBIND,
		C.IP_HDRINCL,
		C.IP_IPSEC_POLICY,
		C.IP_MINTTL,
		C.IP_MSFILTER,
		C.IP_MTU_DISCOVER,
		C.IP_MULTICAST_ALL,
		C.IP_NODEFRAG,
		C.IP_OPTIONS,
		C.IP_PASSSEC,
		C.IP_PKTINFO,
		C.IP_RECVERR,
		C.IP_RECVOPTS,
		C.IP_RECVORIGDSTADDR,
		C.IP_RECVTOS,
		C.IP_RECVTTL,
		C.IP_RETOPTS,
		C.IP_TOS,
		C.IP_TRANSPARENT,
		C.IP_TTL,
		C.IP_UNBLOCK_SOURCE,
		C.IP_UNICAST_IF,
		C.IP_XFRM_POLICY,
		C.MCAST_BLOCK_SOURCE,
		C.MCAST_JOIN_SOURCE_GROUP,
		C.MCAST_LEAVE_GROUP,
		C.MCAST_LEAVE_SOURCE_GROUP,
		C.MCAST_MSFILTER,
		C.MCAST_UNBLOCK_SOURCE:

	default:
		logger.Infof("unimplemented setsockopt: SOL_IP name=%d optVal=%x", name, optVal)

	}
	return tcpip.ErrUnknownProtocolOption
}

// isLinkLocal determines if the given IPv6 address is link-local. This is the
// case when it has the fe80::/10 prefix. This check is used to determine when
// the NICID is relevant for a given IPv6 address.
func isLinkLocal(addr tcpip.Address) bool {
	return len(addr) >= 2 && addr[0] == 0xfe && addr[1]&0xc0 == 0x80
}
