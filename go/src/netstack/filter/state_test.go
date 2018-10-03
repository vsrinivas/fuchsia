// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package filter

import (
	"log"
	"testing"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/header"
)

const debugStateTest = false

const (
	testStateLocalAddrV4  = "\x0a\x00\x00\x00"
	testStateLocalPort    = 100
	testStateRemoteAddrV4 = "\x0a\x00\x00\x02"
	testStateRemotePort   = 200
)

type stateTest struct {
	dir         Direction
	netProto    tcpip.NetworkProtocolNumber
	packet      func() []byte
	localState  EndpointState
	remoteState EndpointState
}

func TestFindStateICMPv4(t *testing.T) {
	var seq1 = []stateTest{
		{
			Outgoing,
			header.IPv4ProtocolNumber,
			func() []byte {
				return icmpV4Packet([]byte("payload"), &icmpV4Params{
					srcAddr:    testStateLocalAddrV4,
					dstAddr:    testStateRemoteAddrV4,
					icmpV4Type: header.ICMPv4EchoReply,
					code:       0,
				})
			},
			ICMPFirstPacket,
			ICMPFirstPacket,
		},
	}

	var tests = [][]stateTest{
		seq1,
	}

	for _, seq := range tests {
		ss := NewStates()

		for _, test := range seq {
			b := test.packet()
			ipv4 := header.IPv4(b)
			if !ipv4.IsValid(len(b)) {
				t.Fatalf("ipv4 packet is not valid: %v", b)
			}
			th := b[ipv4.HeaderLength():]

			srcAddr := ipv4.SourceAddress()
			dstAddr := ipv4.DestinationAddress()
			payloadLength := ipv4.PayloadLength()

			s, err := ss.findStateICMPv4(
				test.dir,
				header.IPv4ProtocolNumber,
				header.ICMPv4ProtocolNumber,
				srcAddr,
				dstAddr,
				payloadLength,
				th,
				nil, // plb
			)
			if err != nil {
				t.Fatalf("err: %v", err)
			}
			if s == nil {
				s = ss.createState(
					test.dir,
					header.ICMPv4ProtocolNumber,
					srcAddr,
					0, // srcPort
					dstAddr,
					0,     // dstPort
					"",    // origAddr
					0,     // origPort
					"",    // rsvdAddr
					0,     // rsvdPort
					false, // isNAT
					false, // isRDR
					payloadLength,
					b,
					th,
					nil, // plb
				)
			}
			if s == nil {
				t.Fatal("createState failed")
			}
			if got, want := s.srcEp.state, test.localState; got != want {
				t.Errorf("localState=%s, want=%s", got, want)
			}
			if got, want := s.dstEp.state, test.remoteState; got != want {
				t.Errorf("remoteState=%s, want=%s", got, want)
			}
		}
	}
}

func TestFindStateUDP(t *testing.T) {
	var seq1 = []stateTest{
		{
			Outgoing,
			header.IPv4ProtocolNumber,
			func() []byte {
				return udpV4Packet([]byte("payload"), &udpParams{
					srcAddr: testStateLocalAddrV4,
					srcPort: testStateLocalPort,
					dstAddr: testStateRemoteAddrV4,
					dstPort: testStateRemotePort,
				})
			},
			UDPSingle,
			UDPFirstPacket,
		},
		{
			Incoming,
			header.IPv4ProtocolNumber,
			func() []byte {
				return udpV4Packet([]byte("payload"), &udpParams{
					srcAddr: testStateRemoteAddrV4,
					srcPort: testStateRemotePort,
					dstAddr: testStateLocalAddrV4,
					dstPort: testStateLocalPort,
				})
			},
			UDPMultiple,
			UDPSingle,
		},
		{
			Outgoing,
			header.IPv4ProtocolNumber,
			func() []byte {
				return udpV4Packet([]byte("payload"), &udpParams{
					srcAddr: testStateLocalAddrV4,
					srcPort: testStateLocalPort,
					dstAddr: testStateRemoteAddrV4,
					dstPort: testStateRemotePort,
				})
			},
			UDPMultiple,
			UDPMultiple,
		},
	}

	var tests = [][]stateTest{
		seq1,
	}

	for _, seq := range tests {
		ss := NewStates()

		for _, test := range seq {
			b := test.packet()
			ipv4 := header.IPv4(b)
			if !ipv4.IsValid(len(b)) {
				t.Fatalf("ipv4 packet is not valid: %v", b)
			}
			th := b[ipv4.HeaderLength():]
			udp := header.UDP(th)

			srcAddr := ipv4.SourceAddress()
			srcPort := udp.SourcePort()
			dstAddr := ipv4.DestinationAddress()
			dstPort := udp.DestinationPort()
			payloadLength := ipv4.PayloadLength()

			s, err := ss.findStateUDP(
				test.dir,
				header.IPv4ProtocolNumber,
				header.UDPProtocolNumber,
				srcAddr,
				srcPort,
				dstAddr,
				dstPort,
				payloadLength,
				th,
			)
			if err != nil {
				t.Fatalf("err: %v", err)
			}
			if s == nil {
				s = ss.createState(
					test.dir,
					header.UDPProtocolNumber,
					srcAddr,
					srcPort,
					dstAddr,
					dstPort,
					"",    // origAddr
					0,     // origPort
					"",    // rsvdAddr
					0,     // rsvdPort
					false, // isNAT
					false, // isRDR
					payloadLength,
					b,
					th,
					nil, // plb
				)
			}
			if s == nil {
				t.Fatal("createState failed")
			}
			if got, want := s.srcEp.state, test.localState; got != want {
				t.Errorf("localState=%s, want=%s", got, want)
			}
			if got, want := s.dstEp.state, test.remoteState; got != want {
				t.Errorf("remoteState=%s, want=%s", got, want)
			}
		}
	}
}

func TestFindStateTCPv4(t *testing.T) {
	var seq1 = []stateTest{
		{
			Outgoing,
			header.IPv4ProtocolNumber,
			func() []byte {
				return tcpV4Packet(nil, &tcpParams{
					srcAddr: testStateLocalAddrV4,
					srcPort: testStateLocalPort,
					dstAddr: testStateRemoteAddrV4,
					dstPort: testStateRemotePort,
					seqNum:  0,
					ackNum:  0,
					flags:   header.TCPFlagSyn,
					rcvWnd:  65535,
				})
			},
			TCPOpening,
			TCPFirstPacket,
		},
		{
			Incoming,
			header.IPv4ProtocolNumber,
			func() []byte {
				return tcpV4Packet(nil, &tcpParams{
					srcAddr: testStateRemoteAddrV4,
					srcPort: testStateRemotePort,
					dstAddr: testStateLocalAddrV4,
					dstPort: testStateLocalPort,
					seqNum:  0,
					ackNum:  1,
					flags:   header.TCPFlagSyn | header.TCPFlagAck,
					rcvWnd:  65535,
				})
			},
			TCPEstablished,
			TCPOpening,
		},
		{
			Outgoing,
			header.IPv4ProtocolNumber,
			func() []byte {
				return tcpV4Packet(nil, &tcpParams{
					srcAddr: testStateLocalAddrV4,
					srcPort: testStateLocalPort,
					dstAddr: testStateRemoteAddrV4,
					dstPort: testStateRemotePort,
					seqNum:  1,
					ackNum:  1,
					flags:   header.TCPFlagAck,
					rcvWnd:  53248,
				})
			},
			TCPEstablished,
			TCPEstablished,
		},
		{
			Outgoing,
			header.IPv4ProtocolNumber,
			func() []byte {
				return tcpV4Packet([]byte("payload"), &tcpParams{
					srcAddr: testStateLocalAddrV4,
					srcPort: testStateLocalPort,
					dstAddr: testStateRemoteAddrV4,
					dstPort: testStateRemotePort,
					seqNum:  1,
					ackNum:  1,
					flags:   header.TCPFlagAck,
					rcvWnd:  53248,
				})
			},
			TCPEstablished,
			TCPEstablished,
		},
		{
			Incoming,
			header.IPv4ProtocolNumber,
			func() []byte {
				return tcpV4Packet(nil, &tcpParams{
					srcAddr: testStateRemoteAddrV4,
					srcPort: testStateRemotePort,
					dstAddr: testStateLocalAddrV4,
					dstPort: testStateLocalPort,
					seqNum:  1,
					ackNum:  8,
					flags:   header.TCPFlagAck,
					rcvWnd:  65535,
				})
			},
			TCPEstablished,
			TCPEstablished,
		},
		{
			Incoming,
			header.IPv4ProtocolNumber,
			func() []byte {
				return tcpV4Packet(nil, &tcpParams{
					srcAddr: testStateRemoteAddrV4,
					srcPort: testStateRemotePort,
					dstAddr: testStateLocalAddrV4,
					dstPort: testStateLocalPort,
					seqNum:  1,
					ackNum:  8,
					flags:   header.TCPFlagFin | header.TCPFlagAck,
					rcvWnd:  65535,
				})
			},
			TCPEstablished,
			TCPClosing,
		},
		{
			Outgoing,
			header.IPv4ProtocolNumber,
			func() []byte {
				return tcpV4Packet(nil, &tcpParams{
					srcAddr: testStateLocalAddrV4,
					srcPort: testStateLocalPort,
					dstAddr: testStateRemoteAddrV4,
					dstPort: testStateRemotePort,
					seqNum:  8,
					ackNum:  2,
					flags:   header.TCPFlagAck,
					rcvWnd:  53248,
				})
			},
			TCPEstablished,
			TCPFinWait,
		},
		{
			Outgoing,
			header.IPv4ProtocolNumber,
			func() []byte {
				return tcpV4Packet(nil, &tcpParams{
					srcAddr: testStateLocalAddrV4,
					srcPort: testStateLocalPort,
					dstAddr: testStateRemoteAddrV4,
					dstPort: testStateRemotePort,
					seqNum:  8,
					ackNum:  2,
					flags:   header.TCPFlagFin | header.TCPFlagAck,
					rcvWnd:  53248,
				})
			},
			TCPClosing,
			TCPFinWait,
		},
		{
			Incoming,
			header.IPv4ProtocolNumber,
			func() []byte {
				return tcpV4Packet(nil, &tcpParams{
					srcAddr: testStateRemoteAddrV4,
					srcPort: testStateRemotePort,
					dstAddr: testStateLocalAddrV4,
					dstPort: testStateLocalPort,
					seqNum:  2,
					ackNum:  9,
					flags:   header.TCPFlagAck,
					rcvWnd:  65535,
				})
			},
			TCPFinWait,
			TCPFinWait,
		},
	}

	var seq2 = []stateTest{
		{
			Outgoing,
			header.IPv4ProtocolNumber,
			func() []byte {
				return tcpV4Packet(nil, &tcpParams{
					srcAddr: testStateLocalAddrV4,
					srcPort: testStateLocalPort,
					dstAddr: testStateRemoteAddrV4,
					dstPort: testStateRemotePort,
					seqNum:  0,
					ackNum:  0,
					flags:   header.TCPFlagSyn,
					tcpOpts: []byte{ // WS: 2
						header.TCPOptionWS, 3, 2, header.TCPOptionNOP,
					},
					rcvWnd: 65535,
				})
			},
			TCPOpening,
			TCPFirstPacket,
		},
		{
			Incoming,
			header.IPv4ProtocolNumber,
			func() []byte {
				return tcpV4Packet(nil, &tcpParams{
					srcAddr: testStateRemoteAddrV4,
					srcPort: testStateRemotePort,
					dstAddr: testStateLocalAddrV4,
					dstPort: testStateLocalPort,
					seqNum:  0,
					ackNum:  1,
					flags:   header.TCPFlagSyn | header.TCPFlagAck,
					tcpOpts: []byte{ // WS: 5
						header.TCPOptionWS, 3, 5, header.TCPOptionNOP,
					},
					rcvWnd: 65535,
				})
			},
			TCPEstablished,
			TCPOpening,
		},
		{
			Outgoing,
			header.IPv4ProtocolNumber,
			func() []byte {
				return tcpV4Packet(nil, &tcpParams{
					srcAddr: testStateLocalAddrV4,
					srcPort: testStateLocalPort,
					dstAddr: testStateRemoteAddrV4,
					dstPort: testStateRemotePort,
					seqNum:  1,
					ackNum:  1,
					flags:   header.TCPFlagAck,
					rcvWnd:  53248,
				})
			},
			TCPEstablished,
			TCPEstablished,
		},
		{
			Outgoing,
			header.IPv4ProtocolNumber,
			func() []byte {
				return tcpV4Packet([]byte("payload"), &tcpParams{
					srcAddr: testStateLocalAddrV4,
					srcPort: testStateLocalPort,
					dstAddr: testStateRemoteAddrV4,
					dstPort: testStateRemotePort,
					seqNum:  1,
					ackNum:  1,
					flags:   header.TCPFlagAck,
					rcvWnd:  53248,
				})
			},
			TCPEstablished,
			TCPEstablished,
		},
		{
			Incoming,
			header.IPv4ProtocolNumber,
			func() []byte {
				return tcpV4Packet(nil, &tcpParams{
					srcAddr: testStateRemoteAddrV4,
					srcPort: testStateRemotePort,
					dstAddr: testStateLocalAddrV4,
					dstPort: testStateLocalPort,
					seqNum:  1,
					ackNum:  8,
					flags:   header.TCPFlagAck,
					rcvWnd:  4122,
				})
			},
			TCPEstablished,
			TCPEstablished,
		},
	}

	var tests = [][]stateTest{
		seq1,
		seq2,
	}

	for _, seq := range tests {
		ss := NewStates()

		for _, test := range seq {
			b := test.packet()
			ipv4 := header.IPv4(b)
			if !ipv4.IsValid(len(b)) {
				t.Fatalf("ipv4 packet is not valid: %v", b)
			}
			th := b[ipv4.HeaderLength():]
			tcp := header.TCP(th)

			srcAddr := ipv4.SourceAddress()
			srcPort := tcp.SourcePort()
			dstAddr := ipv4.DestinationAddress()
			dstPort := tcp.DestinationPort()
			payloadLength := ipv4.PayloadLength()

			s, err := ss.findStateTCP(
				test.dir,
				header.IPv4ProtocolNumber,
				header.TCPProtocolNumber,
				srcAddr,
				srcPort,
				dstAddr,
				dstPort,
				payloadLength,
				th,
			)
			if err != nil {
				t.Fatalf("err: %v", err)
			}
			if s == nil {
				s = ss.createState(
					test.dir,
					header.TCPProtocolNumber,
					srcAddr,
					srcPort,
					dstAddr,
					dstPort,
					"",    // origAddr
					0,     // origPort
					"",    // rsvdAddr
					0,     // rsvdPort
					false, // isNAT
					false, // isRDR
					payloadLength,
					b,
					th,
					nil, // plb
				)
			}
			if s == nil {
				t.Fatal("createState failed")
			}
			if got, want := s.srcEp.state, test.localState; got != want {
				t.Errorf("localState=%s, want=%s", got, want)
			}
			if got, want := s.dstEp.state, test.remoteState; got != want {
				t.Errorf("remoteState=%s, want=%s", got, want)
			}
			if debugStateTest {
				log.Printf("s: %s\n", s)
			}
		}
	}
}
