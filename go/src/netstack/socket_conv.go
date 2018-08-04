// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"encoding/binary"
	"log"
	"time"

	"github.com/google/netstack/tcpip"
)

func isZeros(buf []byte) bool {
	for i := 0; i < len(buf); i++ {
		if buf[i] != 0 {
			return false
		}
	}
	return true
}

func (v *c_mxrio_sockopt_req_reply) intValue() int {
	switch v.optlen {
	case 4:
		return int(binary.LittleEndian.Uint32(v.optval[:]))
	case 8:
		return int(binary.LittleEndian.Uint64(v.optval[:]))
	}
	return 0
}

// TODO: create a tcpip.Option type
func (v *c_mxrio_sockopt_req_reply) Unpack() interface{} {
	switch v.level {
	case SOL_SOCKET:
		switch v.optname {
		case SO_ERROR:
			return tcpip.ErrorOption{}
		case SO_REUSEADDR:
			return tcpip.ReuseAddressOption(v.intValue())
		case SO_KEEPALIVE:
			return tcpip.KeepaliveEnabledOption(v.intValue())
		case SO_BROADCAST:
		case SO_DEBUG:
		case SO_SNDBUF:
		case SO_RCVBUF:
		}
		log.Printf("convSockOpt: TODO SOL_SOCKET optname=%d", v.optname)
	case SOL_IP:
		switch v.optname {
		case IP_TOS:
		case IP_TTL:
		case IP_MULTICAST_IF:
		case IP_MULTICAST_TTL:
			if len(v.optval) < 1 {
				log.Printf("sockopt: bad argument to IP_MULTICAST_TTL")
				return nil
			}
			return tcpip.MulticastTTLOption(v.optval[0])
		case IP_MULTICAST_LOOP:
		case IP_ADD_MEMBERSHIP, IP_DROP_MEMBERSHIP:
			mreqn := c_ip_mreqn{}
			if err := mreqn.Decode(v.optval[:v.optlen]); err != nil {
				// If we fail to decode a c_ip_mreqn, try to decode a c_ip_mreq.
				mreq := c_ip_mreq{}
				if err := mreq.Decode(v.optval[:v.optlen]); err != nil {
					log.Printf("sockopt: bad argument to %d", v.optname)
					return nil
				}
				mreqn.imr_multiaddr = mreq.imr_multiaddr
				mreqn.imr_address = mreq.imr_interface
				mreqn.imr_ifindex = 0
			}
			option := tcpip.MembershipOption{
				NIC:           tcpip.NICID(mreqn.imr_ifindex),
				InterfaceAddr: tcpip.Address(mreqn.imr_address[:]),
				MulticastAddr: tcpip.Address(mreqn.imr_multiaddr[:]),
			}
			if v.optname == IP_ADD_MEMBERSHIP {
				return tcpip.AddMembershipOption(option)
			} else {
				return tcpip.RemoveMembershipOption(option)
			}
		}
		log.Printf("convSockOpt: TODO IPPROTO_IP optname=%d", v.optname)
	case SOL_TCP:
		switch v.optname {
		case TCP_NODELAY:
			return tcpip.NoDelayOption(v.intValue())
		case TCP_INFO:
			return tcpip.InfoOption{}
		case TCP_MAXSEG:
		case TCP_CORK:
		case TCP_KEEPIDLE:
			return tcpip.KeepaliveIdleOption(time.Duration(v.intValue()) * time.Second)
		case TCP_KEEPINTVL:
			return tcpip.KeepaliveIntervalOption(time.Duration(v.intValue()) * time.Second)
		case TCP_KEEPCNT:
			return tcpip.KeepaliveCountOption(v.intValue())
		case TCP_SYNCNT:
		case TCP_LINGER2:
		case TCP_DEFER_ACCEPT:
		case TCP_WINDOW_CLAMP:
		case TCP_QUICKACK:
		}
		log.Printf("convSockOpt: TODO SOL_TCP optname=%d", v.optname)
	}
	return nil
}
