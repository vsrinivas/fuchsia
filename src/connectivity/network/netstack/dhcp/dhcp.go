// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package dhcp implements a DHCP client and server as described in RFC 2131.
package dhcp

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"time"

	"gvisor.dev/gvisor/pkg/tcpip"
)

// Config is standard DHCP configuration.
type Config struct {
	ServerAddress tcpip.Address     // address of the server
	SubnetMask    tcpip.AddressMask // client address subnet mask
	Gateway       tcpip.Address     // client default gateway
	DNS           []tcpip.Address   // client DNS server addresses
	LeaseLength   time.Duration     // length of the address lease
	RenewalTime   time.Duration     // time until client enters RENEWING state
	RebindingTime time.Duration     // time until client enters REBINDING state
}

func (cfg *Config) decode(opts []option) error {
	*cfg = Config{}
	for _, opt := range opts {
		b := opt.body
		if !opt.code.lenValid(len(b)) {
			return fmt.Errorf("%s: bad length: %d", opt.code, len(b))
		}
		switch opt.code {
		case optLeaseTime:
			t := binary.BigEndian.Uint32(b)
			cfg.LeaseLength = time.Duration(t) * time.Second
		case optRenewalTime:
			t := binary.BigEndian.Uint32(b)
			cfg.RenewalTime = time.Duration(t) * time.Second
		case optRebindingTime:
			t := binary.BigEndian.Uint32(b)
			cfg.RebindingTime = time.Duration(t) * time.Second
		case optSubnetMask:
			cfg.SubnetMask = tcpip.AddressMask(b)
		case optDHCPServer:
			cfg.ServerAddress = tcpip.Address(b)
		case optDefaultGateway:
			cfg.Gateway = tcpip.Address(b)
		case optDomainNameServer:
			for ; len(b) > 0; b = b[4:] {
				if len(b) < 4 {
					return fmt.Errorf("DNS bad length: %d", len(b))
				}
				cfg.DNS = append(cfg.DNS, tcpip.Address(b[:4]))
			}
		}
	}
	return nil
}

func (cfg Config) encode() (opts []option) {
	if cfg.ServerAddress != "" {
		opts = append(opts, option{optDHCPServer, []byte(cfg.ServerAddress)})
	}
	if cfg.SubnetMask != "" {
		opts = append(opts, option{optSubnetMask, []byte(cfg.SubnetMask)})
	}
	if cfg.Gateway != "" {
		opts = append(opts, option{optDefaultGateway, []byte(cfg.Gateway)})
	}
	if len(cfg.DNS) > 0 {
		dns := make([]byte, 0, 4*len(cfg.DNS))
		for _, addr := range cfg.DNS {
			dns = append(dns, addr...)
		}
		opts = append(opts, option{optDomainNameServer, dns})
	}
	if l := cfg.LeaseLength / time.Second; l != 0 {
		opts = append(opts, serializeLeaseOption(l, optLeaseTime))
	}
	if r := cfg.RebindingTime / time.Second; r != 0 {
		opts = append(opts, serializeLeaseOption(r, optRebindingTime))
	}
	if r := cfg.RenewalTime / time.Second; r != 0 {
		opts = append(opts, serializeLeaseOption(r, optRenewalTime))
	}
	return opts
}

func serializeLeaseOption(d time.Duration, o optionCode) option {
	v := make([]byte, 4)
	binary.BigEndian.PutUint32(v, uint32(d))
	return option{o, v}
}

const (
	// ServerPort is the well-known UDP port number for a DHCP server.
	ServerPort = 67
	// ClientPort is the well-known UDP port number for a DHCP client.
	ClientPort = 68
)

var magicCookie = []byte{99, 130, 83, 99} // RFC 1497

type xid uint32

type hdr []byte

func (h hdr) init() {
	h[1] = 0x01       // htype
	h[2] = 0x06       // hlen
	h[3] = 0x00       // hops
	h[8], h[9] = 0, 0 // secs
	copy(h[236:240], magicCookie)
}

func (h hdr) isValid() bool {
	if len(h) < 241 {
		return false
	}
	if o := h.op(); o != opRequest && o != opReply {
		return false
	}
	if !bytes.Equal(h[1:3], []byte{0x1, 0x6}) {
		return false
	}
	return bytes.Equal(h[236:240], magicCookie)
}

func (h hdr) op() op           { return op(h[0]) }
func (h hdr) setOp(o op)       { h[0] = byte(o) }
func (h hdr) xidbytes() []byte { return h[4:8] }
func (h hdr) xid() xid         { return xid(h[4])<<24 | xid(h[5])<<16 | xid(h[6])<<8 | xid(h[7]) }
func (h hdr) setBroadcast()    { h[10] |= 1 << 7 }
func (h hdr) broadcast() bool  { return h[10]&1<<7 != 0 }
func (h hdr) ciaddr() []byte   { return h[12:16] }
func (h hdr) yiaddr() []byte   { return h[16:20] }
func (h hdr) siaddr() []byte   { return h[20:24] }
func (h hdr) giaddr() []byte   { return h[24:28] }
func (h hdr) chaddr() []byte   { return h[28:44] }
func (h hdr) sname() []byte    { return h[44:108] }
func (h hdr) file() []byte     { return h[108:236] }

func (h hdr) options() (opts options, err error) {
	i := headerBaseSize
	for i < len(h) {
		if h[i] == 0 {
			i++
			continue
		}
		if h[i] == 255 {
			break
		}
		code := optionCode(h[i])
		i++
		if len(h) < i+1 {
			return nil, fmt.Errorf("option %s missing length i=%d", code, i)
		}
		optlen := int(h[i])
		i++
		if len(h) < i+optlen {
			return nil, fmt.Errorf("option %s too long i=%d, optlen=%d", code, i, optlen)
		}
		opts = append(opts, option{
			code: code,
			body: h[i:][:optlen],
		})
		i += optlen
	}
	return opts, nil
}

func (h hdr) setOptions(opts []option) {
	i := headerBaseSize
	for _, opt := range opts {
		h[i] = byte(opt.code)
		h[i+1] = byte(len(opt.body))
		copy(h[i+2:i+2+len(opt.body)], opt.body)
		i += 2 + len(opt.body)
	}
	h[i] = 255 // End option
	i++
	for ; i < len(h); i++ {
		h[i] = 0
	}
}

// headerBaseSize is the size of a DHCP packet, including the magic cookie.
//
// Note that a DHCP packet is required to have an 'end' option that takes
// up an extra byte, so the minimum DHCP packet size is headerBaseSize + 1.
const headerBaseSize = 240

type option struct {
	code optionCode
	body []byte
}

type optionCode byte

const (
	optSubnetMask       optionCode = 1
	optDefaultGateway   optionCode = 3
	optDomainNameServer optionCode = 6
	optDomainName       optionCode = 15
	optReqIPAddr        optionCode = 50
	optLeaseTime        optionCode = 51
	optDHCPMsgType      optionCode = 53 // dhcpMsgType
	optDHCPServer       optionCode = 54
	optParamReq         optionCode = 55
	optMessage          optionCode = 56
	optRenewalTime      optionCode = 58
	optRebindingTime    optionCode = 59
	optClientID         optionCode = 61
)

func (code optionCode) lenValid(l int) bool {
	switch code {
	case optSubnetMask, optDefaultGateway,
		optReqIPAddr, optLeaseTime, optDHCPServer,
		optRenewalTime, optRebindingTime:
		return l == 4
	case optDHCPMsgType:
		return l == 1
	case optDomainNameServer:
		return l%4 == 0
	case optMessage, optDomainName, optClientID:
		return l >= 1
	case optParamReq:
		return true // no fixed length
	default:
		return true // unknown option, assume ok
	}
}

type options []option

func (opts options) dhcpMsgType() (dhcpMsgType, error) {
	for _, opt := range opts {
		if opt.code == optDHCPMsgType {
			if len(opt.body) != 1 {
				return 0, fmt.Errorf("%s: bad length: %d", opt.code, len(opt.body))
			}
			v := opt.body[0]
			if v <= 0 || v >= 8 {
				return 0, fmt.Errorf("DHCP bad length: %d", len(opt.body))
			}
			return dhcpMsgType(v), nil
		}
	}
	return 0, nil
}

func (opts options) message() string {
	for _, opt := range opts {
		if opt.code == optMessage {
			return string(opt.body)
		}
	}
	return ""
}

func (opts options) len() int {
	l := 0
	for _, opt := range opts {
		l += 1 + 1 + len(opt.body) // code + len + body
	}
	return l + 1 // extra byte for 'pad' option
}

type op byte

const (
	opRequest op = 0x01
	opReply   op = 0x02
)

// dhcpMsgType is the DHCP Message Type from RFC 1533, section 9.4.
type dhcpMsgType byte

const (
	dhcpDISCOVER dhcpMsgType = 1
	dhcpOFFER    dhcpMsgType = 2
	dhcpREQUEST  dhcpMsgType = 3
	dhcpDECLINE  dhcpMsgType = 4
	dhcpACK      dhcpMsgType = 5
	dhcpNAK      dhcpMsgType = 6
	dhcpRELEASE  dhcpMsgType = 7
)
