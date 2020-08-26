// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"math"
	"net"
	"time"

	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	fidlnet "fidl/fuchsia/net"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv6"
	"gvisor.dev/gvisor/pkg/tcpip/transport/tcp"
	"gvisor.dev/gvisor/pkg/tcpip/transport/udp"
)

// TODO(fxbug.dev/44347) We shouldn't need any of this includes after we remove
// C structs from the wire.

// #cgo CFLAGS: -D_GNU_SOURCE
// #cgo CFLAGS: -I${SRCDIR}/../../../../zircon/third_party/ulib/musl/include/
// #include <netinet/in.h>
// #include <netinet/tcp.h>
// #include <netinet/udp.h>
import "C"

// Functions below are adapted from
// https://github.com/google/gvisor/blob/master/pkg/sentry/socket/netstack/netstack.go
//
// At the time of writing, this command produces a reasonable diff:
//
/*
   curl -sfSL https://raw.githubusercontent.com/google/gvisor/master/pkg/sentry/socket/netstack/netstack.go |
   sed s/linux/C/g | \
   sed 's/, outLen)/)/g' | \
   sed 's/(t, /(/g' | \
   sed 's/(s, /(/g' | \
   sed 's/, family,/,/g' | \
   sed 's/, skType,/, transProto,/g' | \
   diff --color --ignore-all-space --unified - src/connectivity/network/netstack/socket_conv.go
*/

const (
	// DefaultTTL is linux's default TTL. All network protocols in all stacks used
	// with this package must have this value set as their default TTL.
	DefaultTTL = 64

	sizeOfInt32 int = 4

	// Max values for sockopt TCP_KEEPIDLE and TCP_KEEPINTVL in Linux.
	//
	// https://github.com/torvalds/linux/blob/f2850dd5ee015bd7b77043f731632888887689c7/include/net/tcp.h#L156-L158
	maxTCPKeepIdle  = 32767
	maxTCPKeepIntvl = 32767
	maxTCPKeepCnt   = 127
)

func boolToInt32(v bool) int32 {
	if v {
		return 1
	}
	return 0
}

func GetSockOpt(ep tcpip.Endpoint, ns *Netstack, netProto tcpip.NetworkProtocolNumber, transProto tcpip.TransportProtocolNumber, level, name int16) (interface{}, *tcpip.Error) {
	switch level {
	case C.SOL_SOCKET:
		return getSockOptSocket(ep, ns, netProto, transProto, name)

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
		syslog.Infof("unimplemented getsockopt: level=%d name=%d", level, name)

	}
	return nil, tcpip.ErrUnknownProtocol
}

func getSockOptSocket(ep tcpip.Endpoint, ns *Netstack, netProto tcpip.NetworkProtocolNumber, transProto tcpip.TransportProtocolNumber, name int16) (interface{}, *tcpip.Error) {
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

	case C.SO_PROTOCOL:
		switch transProto {
		case tcp.ProtocolNumber:
			return int32(C.IPPROTO_TCP), nil
		case udp.ProtocolNumber:
			return int32(C.IPPROTO_UDP), nil
		case header.ICMPv4ProtocolNumber:
			return int32(C.IPPROTO_ICMP), nil
		case header.ICMPv6ProtocolNumber:
			return int32(C.IPPROTO_ICMPV6), nil
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
		v, err := ep.GetSockOptBool(tcpip.PasscredOption)
		if err != nil {
			return nil, err
		}

		return boolToInt32(v), nil

	case C.SO_SNDBUF:
		size, err := ep.GetSockOptInt(tcpip.SendBufferSizeOption)
		if err != nil {
			return nil, err
		}

		if size > math.MaxInt32 {
			size = math.MaxInt32
		}

		return int32(size), nil

	case C.SO_RCVBUF:
		size, err := ep.GetSockOptInt(tcpip.ReceiveBufferSizeOption)
		if err != nil {
			return nil, err
		}

		if size > math.MaxInt32 {
			size = math.MaxInt32
		}

		return int32(size), nil

	case C.SO_REUSEADDR:
		v, err := ep.GetSockOptBool(tcpip.ReuseAddressOption)
		if err != nil {
			return nil, err
		}

		return boolToInt32(v), nil

	case C.SO_REUSEPORT:
		v, err := ep.GetSockOptBool(tcpip.ReusePortOption)
		if err != nil {
			return nil, err
		}

		return boolToInt32(v), nil

	case C.SO_BINDTODEVICE:
		var v tcpip.BindToDeviceOption
		if err := ep.GetSockOpt(&v); err != nil {
			return nil, err
		}
		if v == tcpip.BindToDeviceOption(0) {
			return []byte(nil), nil
		}
		nicInfos := ns.stack.NICInfo()
		for id, info := range nicInfos {
			if tcpip.BindToDeviceOption(id) == v {
				return append([]byte(info.Name), 0), nil
			}
		}
		return nil, tcpip.ErrUnknownDevice

	case C.SO_BROADCAST:
		v, err := ep.GetSockOptBool(tcpip.BroadcastOption)
		if err != nil {
			return nil, err
		}

		return boolToInt32(v), nil

	case C.SO_KEEPALIVE:
		v, err := ep.GetSockOptBool(tcpip.KeepaliveEnabledOption)
		if err != nil {
			return nil, err
		}

		return boolToInt32(v), nil

	case C.SO_LINGER:
		// TODO(tamird): Remove this when upstream supports UDP correctly.
		if transProto != tcp.ProtocolNumber {
			return C.struct_linger{}, nil
		}
		var v tcpip.LingerOption
		if err := ep.GetSockOpt(&v); err != nil {
			return nil, err
		}
		linger := C.struct_linger{
			l_linger: C.int(v.Timeout.Seconds()),
		}
		if v.Enabled {
			linger.l_onoff = 1
		}

		return linger, nil

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

	case C.SO_NO_CHECK:
		v, err := ep.GetSockOptBool(tcpip.NoChecksumOption)
		if err != nil {
			return nil, err
		}

		return boolToInt32(v), nil

	default:
		syslog.Infof("unimplemented getsockopt: SOL_SOCKET name=%d", name)

	}
	return nil, tcpip.ErrUnknownProtocolOption
}

func getSockOptTCP(ep tcpip.Endpoint, name int16) (interface{}, *tcpip.Error) {
	switch name {
	case C.TCP_NODELAY:
		v, err := ep.GetSockOptBool(tcpip.DelayOption)
		if err != nil {
			return nil, err
		}

		return boolToInt32(!v), nil

	case C.TCP_CORK:
		v, err := ep.GetSockOptBool(tcpip.CorkOption)
		if err != nil {
			return nil, err
		}

		return boolToInt32(v), nil

	case C.TCP_QUICKACK:
		v, err := ep.GetSockOptBool(tcpip.QuickAckOption)
		if err != nil {
			return nil, err
		}

		return boolToInt32(v), nil

	case C.TCP_MAXSEG:
		v, err := ep.GetSockOptInt(tcpip.MaxSegOption)
		if err != nil {
			return nil, err
		}

		return int32(v), nil

	case C.TCP_KEEPIDLE:
		var v tcpip.KeepaliveIdleOption
		if err := ep.GetSockOpt(&v); err != nil {
			return nil, err
		}

		return int32(time.Duration(v).Seconds()), nil

	case C.TCP_KEEPINTVL:
		var v tcpip.KeepaliveIntervalOption
		if err := ep.GetSockOpt(&v); err != nil {
			return nil, err
		}

		return int32(time.Duration(v).Seconds()), nil

	case C.TCP_KEEPCNT:
		v, err := ep.GetSockOptInt(tcpip.KeepaliveCountOption)
		if err != nil {
			return nil, err
		}

		return int32(v), nil

	case C.TCP_USER_TIMEOUT:
		var v tcpip.TCPUserTimeoutOption
		if err := ep.GetSockOpt(&v); err != nil {
			return nil, err
		}

		return int32(time.Duration(v).Milliseconds()), nil

	case C.TCP_CONGESTION:
		var v tcpip.CongestionControlOption
		if err := ep.GetSockOpt(&v); err != nil {
			return nil, err
		}
		// https://github.com/torvalds/linux/blob/f2850dd5ee015bd7b77043f731632888887689c7/include/net/tcp.h#L1012
		tcpCANameMax := 16
		// TODO(fxb/41621): should we change getsocketopt fidl to pass optlen?
		b := append([]byte(v), 0)
		// Linux uses min(optlen, TCP_CA_NAME_MAX) for length of returned name.
		//
		// https://github.com/torvalds/linux/blob/33b40134e5cfbbccad7f3040d1919889537a3df7/net/ipv4/tcp.c#L3502
		if len(b) > tcpCANameMax {
			b = b[:tcpCANameMax]
		}
		return b, nil

	case C.TCP_DEFER_ACCEPT:
		var v tcpip.TCPDeferAcceptOption
		if err := ep.GetSockOpt(&v); err != nil {
			return nil, err
		}

		return int32(time.Duration(v).Seconds()), nil

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

	case C.TCP_SYNCNT:
		v, err := ep.GetSockOptInt(tcpip.TCPSynCountOption)
		if err != nil {
			return nil, err
		}
		return int32(v), nil

	case C.TCP_WINDOW_CLAMP:
		v, err := ep.GetSockOptInt(tcpip.TCPWindowClampOption)
		if err != nil {
			return nil, err
		}
		return int32(v), nil

	case C.TCP_LINGER2:
		var v tcpip.TCPLingerTimeoutOption
		if err := ep.GetSockOpt(&v); err != nil {
			return nil, err
		}
		// Linux uses this socket option to override `tcp_fin_timeout`, which is in
		// seconds.
		//
		// See the man page for details: https://man7.org/linux/man-pages/man7/tcp.7.html
		return int32(time.Duration(v) / time.Second), nil

	case
		C.TCP_CC_INFO,
		C.TCP_NOTSENT_LOWAT:

	default:
		syslog.Infof("unimplemented getsockopt: SOL_TCP name=%d", name)

	}
	return nil, tcpip.ErrUnknownProtocolOption
}

func getSockOptIPv6(ep tcpip.Endpoint, name int16) (interface{}, *tcpip.Error) {
	switch name {
	case C.IPV6_V6ONLY:
		v, err := ep.GetSockOptBool(tcpip.V6OnlyOption)
		if err != nil {
			return nil, err
		}
		return boolToInt32(v), nil

	case C.IPV6_PATHMTU:

	case C.IPV6_TCLASS:
		v, err := ep.GetSockOptInt(tcpip.IPv6TrafficClassOption)
		if err != nil {
			return nil, err
		}
		return int32(v), nil

	case C.IPV6_MULTICAST_IF:
		var v tcpip.MulticastInterfaceOption
		if err := ep.GetSockOpt(&v); err != nil {
			return nil, err
		}

		return int32(v.NIC), nil

	case C.IPV6_MULTICAST_HOPS:
		v, err := ep.GetSockOptInt(tcpip.MulticastTTLOption)
		if err != nil {
			return nil, err
		}
		return int32(v), nil

	case C.IPV6_MULTICAST_LOOP:
		v, err := ep.GetSockOptBool(tcpip.MulticastLoopOption)
		if err != nil {
			return nil, err
		}
		return boolToInt32(v), nil

	case C.IPV6_RECVTCLASS:
		v, err := ep.GetSockOptBool(tcpip.ReceiveTClassOption)
		if err != nil {
			return nil, err
		}
		return boolToInt32(v), nil

	default:
		syslog.Infof("unimplemented getsockopt: SOL_IPV6 name=%d", name)

	}
	return nil, tcpip.ErrUnknownProtocolOption
}

func getSockOptIP(ep tcpip.Endpoint, name int16) (interface{}, *tcpip.Error) {
	switch name {
	case C.IP_TTL:
		v, err := ep.GetSockOptInt(tcpip.TTLOption)
		if err != nil {
			return nil, err
		}

		// Fill in default value, if needed.
		if v == 0 {
			v = DefaultTTL
		}

		return int32(v), nil

	case C.IP_MULTICAST_TTL:
		v, err := ep.GetSockOptInt(tcpip.MulticastTTLOption)
		if err != nil {
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
		v, err := ep.GetSockOptBool(tcpip.MulticastLoopOption)
		if err != nil {
			return nil, err
		}

		return boolToInt32(v), nil

	case C.IP_TOS:
		v, err := ep.GetSockOptInt(tcpip.IPv4TOSOption)
		if err != nil {
			return nil, err
		}
		return int32(v), nil

	case C.IP_RECVTOS:
		v, err := ep.GetSockOptBool(tcpip.ReceiveTOSOption)
		if err != nil {
			return nil, err
		}
		return boolToInt32(v), nil

	case C.IP_PKTINFO:
		v, err := ep.GetSockOptBool(tcpip.ReceiveIPPacketInfoOption)
		if err != nil {
			return nil, err
		}
		return boolToInt32(v), nil

	default:
		syslog.Infof("unimplemented getsockopt: SOL_IP name=%d", name)

	}
	return nil, tcpip.ErrUnknownProtocolOption
}

func SetSockOpt(ep tcpip.Endpoint, ns *Netstack, level, name int16, optVal []uint8) *tcpip.Error {
	switch level {
	case C.SOL_SOCKET:
		return setSockOptSocket(ep, ns, name, optVal)

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
		syslog.Infof("unimplemented setsockopt: level=%d name=%d optVal=%x", level, name, optVal)

	}
	return tcpip.ErrUnknownProtocolOption
}

func setSockOptSocket(ep tcpip.Endpoint, ns *Netstack, name int16, optVal []byte) *tcpip.Error {
	switch name {
	case C.SO_SNDBUF:
		if len(optVal) < sizeOfInt32 {
			return tcpip.ErrInvalidOptionValue
		}

		v := binary.LittleEndian.Uint32(optVal)
		return ep.SetSockOptInt(tcpip.SendBufferSizeOption, int(v))

	case C.SO_RCVBUF:
		if len(optVal) < sizeOfInt32 {
			return tcpip.ErrInvalidOptionValue
		}

		v := binary.LittleEndian.Uint32(optVal)
		return ep.SetSockOptInt(tcpip.ReceiveBufferSizeOption, int(v))

	case C.SO_REUSEADDR:
		if len(optVal) < sizeOfInt32 {
			return tcpip.ErrInvalidOptionValue
		}

		v := binary.LittleEndian.Uint32(optVal)
		return ep.SetSockOptBool(tcpip.ReuseAddressOption, v != 0)

	case C.SO_REUSEPORT:
		if len(optVal) < sizeOfInt32 {
			return tcpip.ErrInvalidOptionValue
		}

		v := binary.LittleEndian.Uint32(optVal)
		return ep.SetSockOptBool(tcpip.ReusePortOption, v != 0)

	case C.SO_BINDTODEVICE:
		n := bytes.IndexByte(optVal, 0)
		if n == -1 {
			n = len(optVal)
		}
		if n == 0 {
			return ep.SetSockOpt(tcpip.BindToDeviceOption(0))
		}
		name := string(optVal[:n])
		nicInfos := ns.stack.NICInfo()
		for id, info := range nicInfos {
			if name == info.Name {
				return ep.SetSockOpt(tcpip.BindToDeviceOption(id))
			}
		}
		return tcpip.ErrUnknownDevice

	case C.SO_BROADCAST:
		if len(optVal) < sizeOfInt32 {
			return tcpip.ErrInvalidOptionValue
		}

		v := binary.LittleEndian.Uint32(optVal)
		return ep.SetSockOptBool(tcpip.BroadcastOption, v != 0)

	case C.SO_PASSCRED:
		if len(optVal) < sizeOfInt32 {
			return tcpip.ErrInvalidOptionValue
		}

		v := binary.LittleEndian.Uint32(optVal)
		return ep.SetSockOptBool(tcpip.PasscredOption, v != 0)

	case C.SO_KEEPALIVE:
		if len(optVal) < sizeOfInt32 {
			return tcpip.ErrInvalidOptionValue
		}

		v := binary.LittleEndian.Uint32(optVal)
		return ep.SetSockOptBool(tcpip.KeepaliveEnabledOption, v != 0)

	case C.SO_LINGER:
		var linger C.struct_linger
		if err := linger.Unmarshal(optVal); err != nil {
			return tcpip.ErrInvalidOptionValue
		}
		return ep.SetSockOpt(tcpip.LingerOption{
			Enabled: linger.l_onoff != 0,
			Timeout: time.Second * time.Duration(linger.l_linger),
		})

	case C.SO_SNDTIMEO:
		return tcpip.ErrNotSupported

	case C.SO_RCVTIMEO:
		return tcpip.ErrNotSupported

	case C.SO_OOBINLINE:
		if len(optVal) < sizeOfInt32 {
			return tcpip.ErrInvalidOptionValue
		}

		v := binary.LittleEndian.Uint32(optVal)
		return ep.SetSockOpt(tcpip.OutOfBandInlineOption(v))

	case C.SO_NO_CHECK:
		if len(optVal) < sizeOfInt32 {
			return tcpip.ErrInvalidOptionValue
		}

		v := binary.LittleEndian.Uint32(optVal)
		return ep.SetSockOptBool(tcpip.NoChecksumOption, v != 0)

	default:
		syslog.Infof("unimplemented setsockopt: SOL_SOCKET name=%d optVal=%x", name, optVal)

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
		return ep.SetSockOptBool(tcpip.DelayOption, v == 0)

	case C.TCP_CORK:
		if len(optVal) < sizeOfInt32 {
			return tcpip.ErrInvalidOptionValue
		}

		v := binary.LittleEndian.Uint32(optVal)
		return ep.SetSockOptBool(tcpip.CorkOption, v != 0)

	case C.TCP_QUICKACK:
		if len(optVal) < sizeOfInt32 {
			return tcpip.ErrInvalidOptionValue
		}

		v := binary.LittleEndian.Uint32(optVal)
		return ep.SetSockOptBool(tcpip.QuickAckOption, v != 0)

	case C.TCP_MAXSEG:
		if len(optVal) < sizeOfInt32 {
			return tcpip.ErrInvalidOptionValue
		}

		v := binary.LittleEndian.Uint32(optVal)
		return ep.SetSockOptInt(tcpip.MaxSegOption, int(v))

	case C.TCP_KEEPIDLE:
		if len(optVal) < sizeOfInt32 {
			return tcpip.ErrInvalidOptionValue
		}

		v := binary.LittleEndian.Uint32(optVal)
		// https://github.com/torvalds/linux/blob/f2850dd5ee015bd7b77043f731632888887689c7/net/ipv4/tcp.c#L2991
		if v < 1 || v > maxTCPKeepIdle {
			return tcpip.ErrInvalidOptionValue
		}
		return ep.SetSockOpt(tcpip.KeepaliveIdleOption(time.Second * time.Duration(v)))

	case C.TCP_KEEPINTVL:
		if len(optVal) < sizeOfInt32 {
			return tcpip.ErrInvalidOptionValue
		}

		v := binary.LittleEndian.Uint32(optVal)
		// https://github.com/torvalds/linux/blob/f2850dd5ee015bd7b77043f731632888887689c7/net/ipv4/tcp.c#L3008
		if v < 1 || v > maxTCPKeepIntvl {
			return tcpip.ErrInvalidOptionValue
		}
		return ep.SetSockOpt(tcpip.KeepaliveIntervalOption(time.Second * time.Duration(v)))

	case C.TCP_KEEPCNT:
		if len(optVal) < sizeOfInt32 {
			return tcpip.ErrInvalidOptionValue
		}

		v := binary.LittleEndian.Uint32(optVal)
		// https://github.com/torvalds/linux/blob/f2850dd5ee015bd7b77043f731632888887689c7/net/ipv4/tcp.c#L3014
		if v < 1 || v > maxTCPKeepCnt {
			return tcpip.ErrInvalidOptionValue
		}
		return ep.SetSockOptInt(tcpip.KeepaliveCountOption, int(v))

	case C.TCP_USER_TIMEOUT:
		if len(optVal) < sizeOfInt32 {
			return tcpip.ErrInvalidOptionValue
		}
		v := int32(binary.LittleEndian.Uint32(optVal))
		// https://github.com/torvalds/linux/blob/33b40134e5cfbbccad7f3040d1919889537a3df7/net/ipv4/tcp.c#L3086-L3094
		if v < 0 {
			return tcpip.ErrInvalidOptionValue
		}
		return ep.SetSockOpt(tcpip.TCPUserTimeoutOption(time.Millisecond * time.Duration(v)))

	case C.TCP_CONGESTION:
		return ep.SetSockOpt(tcpip.CongestionControlOption(optVal))

	case C.TCP_DEFER_ACCEPT:
		if len(optVal) < sizeOfInt32 {
			return tcpip.ErrInvalidOptionValue
		}
		v := int32(binary.LittleEndian.Uint32(optVal))
		// Use 0 if negative to match Linux.
		//
		// https://github.com/torvalds/linux/blob/33b40134e5cfbbccad7f3040d1919889537a3df7/net/ipv4/tcp.c#L3045
		if v < 0 {
			v = 0
		}
		return ep.SetSockOpt(tcpip.TCPDeferAcceptOption(time.Second * time.Duration(v)))

	case C.TCP_SYNCNT:
		if len(optVal) < sizeOfInt32 {
			return tcpip.ErrInvalidOptionValue
		}
		return ep.SetSockOptInt(tcpip.TCPSynCountOption, int(binary.LittleEndian.Uint32(optVal)))

	case C.TCP_WINDOW_CLAMP:
		if len(optVal) < sizeOfInt32 {
			return tcpip.ErrInvalidOptionValue
		}
		return ep.SetSockOptInt(tcpip.TCPWindowClampOption, int(binary.LittleEndian.Uint32(optVal)))

	case C.TCP_LINGER2:
		if len(optVal) < sizeOfInt32 {
			return tcpip.ErrInvalidOptionValue
		}
		v := binary.LittleEndian.Uint32(optVal)
		// Linux uses this socket option to override `tcp_fin_timeout`, which is in
		// seconds.
		//
		// See the man page for details: https://man7.org/linux/man-pages/man7/tcp.7.html
		return ep.SetSockOpt(tcpip.TCPLingerTimeoutOption(time.Second * time.Duration(v)))

	case C.TCP_REPAIR_OPTIONS:

	default:
		syslog.Infof("unimplemented setsockopt: SOL_TCP name=%d optVal=%x", name, optVal)

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
		return ep.SetSockOptBool(tcpip.V6OnlyOption, v != 0)

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

	case C.IPV6_MULTICAST_IF:
		v, err := parseIntOrChar(optVal)
		if err != nil {
			return err
		}
		return ep.SetSockOpt(tcpip.MulticastInterfaceOption{
			NIC: tcpip.NICID(v),
		})

	case C.IPV6_MULTICAST_HOPS:
		if len(optVal) < sizeOfInt32 {
			return tcpip.ErrInvalidOptionValue
		}

		v, err := parseIntOrChar(optVal)
		if err != nil {
			return err
		}

		if v == -1 {
			// Linux translates -1 to 1.
			v = 1
		}

		if v < 0 || v > 255 {
			return tcpip.ErrInvalidOptionValue
		}

		return ep.SetSockOptInt(tcpip.MulticastTTLOption, int(v))

	case C.IPV6_MULTICAST_LOOP:
		if len(optVal) < sizeOfInt32 {
			return tcpip.ErrInvalidOptionValue
		}

		v, err := parseIntOrChar(optVal)
		if err != nil {
			return err
		}
		return ep.SetSockOptBool(tcpip.MulticastLoopOption, v != 0)

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

	case C.IPV6_TCLASS:
		if len(optVal) < sizeOfInt32 {
			return tcpip.ErrInvalidOptionValue
		}
		v := int32(binary.LittleEndian.Uint32(optVal))
		if v < -1 || v > 255 {
			return tcpip.ErrInvalidOptionValue
		}
		if v == -1 {
			v = 0
		}
		return ep.SetSockOptInt(tcpip.IPv6TrafficClassOption, int(v))

	case C.IPV6_RECVTCLASS:
		// Although this is a boolean int flag, linux enforces that it is not
		// a char. This is a departure for how this is handled for the
		// comparable IPv4 option.
		// https://github.com/torvalds/linux/blob/f2850dd5ee015bd7b77043f731632888887689c7/net/ipv6/ipv6_sockglue.c#L345
		if len(optVal) < sizeOfInt32 {
			return tcpip.ErrInvalidOptionValue
		}
		v, err := parseIntOrChar(optVal)
		if err != nil {
			return err
		}
		return ep.SetSockOptBool(tcpip.ReceiveTClassOption, v != 0)

	default:
		syslog.Infof("unimplemented setsockopt: SOL_IPV6 name=%d optVal=%x", name, optVal)

	}
	return tcpip.ErrUnknownProtocolOption
}

// parseIntOrChar copies either a 32-bit int or an 8-bit uint out of buf.
//
// net/ipv4/ip_sockglue.c:do_ip_setsockopt does this for its socket options.
func parseIntOrChar(buf []byte) (int32, *tcpip.Error) {
	if len(buf) == 0 {
		return 0, tcpip.ErrInvalidOptionValue
	}

	if len(buf) >= sizeOfInt32 {
		return int32(binary.LittleEndian.Uint32(buf)), nil
	}

	return int32(buf[0]), nil
}

// setSockOptIP implements SetSockOpt when level is SOL_IP.
func setSockOptIP(ep tcpip.Endpoint, name int16, optVal []byte) *tcpip.Error {
	switch name {
	case C.IP_MULTICAST_TTL:
		v, err := parseIntOrChar(optVal)
		if err != nil {
			return err
		}

		if v == -1 {
			// Linux translates -1 to 1.
			v = 1
		}

		if v < 0 || v > 255 {
			return tcpip.ErrInvalidOptionValue
		}

		return ep.SetSockOptInt(tcpip.MulticastTTLOption, int(v))

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
		v, err := parseIntOrChar(optVal)
		if err != nil {
			return err
		}

		return ep.SetSockOptBool(tcpip.MulticastLoopOption, v != 0)

	case C.MCAST_JOIN_GROUP:
		// FIXME: Disallow IP-level multicast group options by
		// default. These will need to be supported by appropriately plumbing
		// the level through to the network stack (if at all). However, we
		// still allow setting TTL, and multicast-enable/disable type options.
		return tcpip.ErrNotSupported

	case C.IP_TTL:
		v, err := parseIntOrChar(optVal)
		if err != nil {
			return err
		}
		// -1 means default TTL.
		if v == -1 {
			v = 0
		} else if v < 1 || v > 255 {
			return tcpip.ErrInvalidOptionValue
		}
		return ep.SetSockOptInt(tcpip.TTLOption, int(v))

	case C.IP_TOS:
		if len(optVal) == 0 {
			return nil
		}
		v, err := parseIntOrChar(optVal)
		if err != nil {
			return err
		}
		return ep.SetSockOptInt(tcpip.IPv4TOSOption, int(v))

	case C.IP_RECVTOS:
		v, err := parseIntOrChar(optVal)
		if err != nil {
			return err
		}
		return ep.SetSockOptBool(tcpip.ReceiveTOSOption, v != 0)

	case C.IP_PKTINFO:
		if len(optVal) == 0 {
			return nil
		}
		v, err := parseIntOrChar(optVal)
		if err != nil {
			return err
		}
		return ep.SetSockOptBool(tcpip.ReceiveIPPacketInfoOption, v != 0)

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
		C.IP_RECVERR,
		C.IP_RECVOPTS,
		C.IP_RECVORIGDSTADDR,
		C.IP_RECVTTL,
		C.IP_RETOPTS,
		C.IP_TRANSPARENT,
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
		syslog.Infof("unimplemented setsockopt: SOL_IP name=%d optVal=%x", name, optVal)

	}
	return tcpip.ErrUnknownProtocolOption
}

// isLinkLocal determines if the given IPv6 address is link-local. This is the
// case when it has the fe80::/10 prefix. This check is used to determine when
// the NICID is relevant for a given IPv6 address.
func isLinkLocal(addr fidlnet.Ipv6Address) bool {
	return addr.Addr[0] == 0xfe && addr.Addr[1]&0xc0 == 0x80
}

// toNetSocketAddress converts a tcpip.FullAddress into a fidlnet.SocketAddress
// taking the protocol into consideration. If addr is unspecified, the
// unspecified address for the provided protocol is returned.
//
// Panics if protocol is neither IPv4 nor IPv6.
func toNetSocketAddress(protocol tcpip.NetworkProtocolNumber, addr tcpip.FullAddress) fidlnet.SocketAddress {
	switch protocol {
	case ipv4.ProtocolNumber:
		out := fidlnet.Ipv4SocketAddress{
			Port: addr.Port,
		}
		copy(out.Address.Addr[:], addr.Addr)
		return fidlnet.SocketAddressWithIpv4(out)
	case ipv6.ProtocolNumber:
		out := fidlnet.Ipv6SocketAddress{
			Port: addr.Port,
		}
		if len(addr.Addr) == header.IPv4AddressSize {
			// Copy address in v4-mapped format.
			copy(out.Address.Addr[header.IPv6AddressSize-header.IPv4AddressSize:], addr.Addr)
			out.Address.Addr[header.IPv6AddressSize-header.IPv4AddressSize-1] = 0xff
			out.Address.Addr[header.IPv6AddressSize-header.IPv4AddressSize-2] = 0xff
		} else {
			copy(out.Address.Addr[:], addr.Addr)
			if isLinkLocal(out.Address) {
				out.ZoneIndex = uint64(addr.NIC)
			}
		}
		return fidlnet.SocketAddressWithIpv6(out)
	default:
		panic(fmt.Sprintf("invalid protocol for conversion: %d", protocol))
	}
}

func toTCPIPFullAddress(addr fidlnet.SocketAddress) (tcpip.FullAddress, error) {
	skipZeros := func(b []uint8) tcpip.Address {
		if isZeros(b) {
			return ""
		}
		return tcpip.Address(b)
	}
	switch addr.Which() {
	case fidlnet.SocketAddressIpv4:
		return tcpip.FullAddress{
			NIC:  0,
			Addr: skipZeros(addr.Ipv4.Address.Addr[:]),
			Port: addr.Ipv4.Port,
		}, nil
	case fidlnet.SocketAddressIpv6:
		return tcpip.FullAddress{
			NIC:  tcpip.NICID(addr.Ipv6.ZoneIndex),
			Addr: skipZeros(addr.Ipv6.Address.Addr[:]),
			Port: addr.Ipv6.Port,
		}, nil
	default:
		return tcpip.FullAddress{}, fmt.Errorf("invalid fuchsia.net/SocketAddress variant: %d", addr.Which())
	}
}
