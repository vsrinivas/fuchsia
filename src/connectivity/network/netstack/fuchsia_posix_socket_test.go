// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package netstack

import (
	"context"
	"fmt"
	"syscall/zx"
	"testing"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/udp_serde"
	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/waiter"
)

func TestDatagramSocketWithBlockingEndpoint(t *testing.T) {
	addGoleakCheck(t)
	for _, test := range []struct {
		name              string
		closeWhileBlocked bool
	}{
		{name: "closeWhileBlocked", closeWhileBlocked: true},
		{name: "closeAfterUnblocked", closeWhileBlocked: false},
	} {
		t.Run(test.name, func(t *testing.T) {
			ns, _ := newNetstack(t, netstackTestOptions{})
			linkEp := &sentinelEndpoint{}
			linkEp.SetBlocking(true)
			ifState := installAndValidateIface(t, ns, func(t *testing.T, ns *Netstack, name string) *ifState {
				return addLinkEndpoint(t, ns, name, linkEp)
			})
			t.Cleanup(ifState.RemoveByUser)

			addr := tcpip.ProtocolAddress{
				Protocol: ipv4.ProtocolNumber,
				AddressWithPrefix: tcpip.AddressWithPrefix{
					Address:   tcpip.Address("\xf0\xf0\xf0\xf0"),
					PrefixLen: 24,
				},
			}
			addAddressAndRoute(t, ns, ifState, addr)

			wq := new(waiter.Queue)
			ep := func() tcpip.Endpoint {
				ep, err := ns.stack.NewEndpoint(header.UDPProtocolNumber, ipv4.ProtocolNumber, wq)
				if err != nil {
					t.Fatalf("NewEndpoint(header.UDPProtocolNumber, ipv4.ProtocolNumber, _) = %s", err)
				}
				return ep
			}()

			s, err := newDatagramSocketImpl(ns, header.UDPProtocolNumber, ipv4.ProtocolNumber, ep, wq)
			if err != nil {
				t.Fatalf("got newDatagramSocketImpl(_, %d, %d, _, _): %s", header.UDPProtocolNumber, ipv4.ProtocolNumber, err)
			}

			// Increment refcount and provide a cancel callback so the endpoint can be
			// closed below.
			s.endpoint.incRef()
			ctx, cancel := context.WithCancel(context.Background())
			s.cancel = cancel

			io, err := s.Describe(context.Background())
			if err != nil {
				t.Fatalf("got s.Describe(): %s", err)
			}

			data := []byte{0, 1, 2, 3, 4}
			preludeSize := io.TxMetaBufSize
			buf := make([]byte, len(data)+int(preludeSize))

			toAddr := &tcpip.FullAddress{
				Addr: addr.AddressWithPrefix.Address,
				Port: 42,
			}
			if err := udp_serde.SerializeSendMsgMeta(
				ipv4.ProtocolNumber,
				*toAddr,
				tcpip.SendableControlMessages{},
				buf[:preludeSize],
			); err != nil {
				t.Fatalf("SerializeSendMsgAddress(%d, %#v, _): %s", ipv4.ProtocolNumber, toAddr, err)
			}
			copy(buf[preludeSize:], data)

			writeUntilBlocked := func() uint {
				written := 0
				for {
					n, err := io.Socket.Write(buf, 0)
					if err == nil {
						if got, want := n, len(buf); got != want {
							t.Fatalf("got zx.socket.Write(_) = (%d, %s), want (%d, nil)", got, err, want)
						}
						written += 1
					} else {
						if err, ok := err.(*zx.Error); ok && err.Status == zx.ErrShouldWait {
							break
						}
						t.Fatalf("got zx.socket.Write(_) = (_, %s), want (_, %s)", err, zx.ErrShouldWait)
					}
				}
				return uint(written)
			}

			const numPayloadsFittingInSendBuf = 10
			bytesPerPayload := len(data) + header.UDPMinimumSize + header.IPv4MaximumHeaderSize
			ep.SocketOptions().SetSendBufferSize(int64(numPayloadsFittingInSendBuf*bytesPerPayload), false)

			var enqueuedSoFar uint
			expectLinkEpEnqueued := func(expected uint) error {
				if got, want := linkEp.Enqueued(), expected+enqueuedSoFar; got != want {
					return fmt.Errorf("got linkEp.Enqueued() = %d, want %d", got, want)
				}
				enqueuedSoFar += expected
				return nil
			}

			// Expect that the sender becomes blocked once the link endpoint has enqueued
			// enough payloads to exhaust the send buffer.

			inflightPayloads := func() uint {
				waiter := linkEp.WaitFor(numPayloadsFittingInSendBuf)
				inflightPayloads := writeUntilBlocked()
				if inflightPayloads < numPayloadsFittingInSendBuf {
					t.Fatalf("wrote %d payloads, want at least %d", inflightPayloads, numPayloadsFittingInSendBuf)
				}
				<-waiter
				if err := expectLinkEpEnqueued(numPayloadsFittingInSendBuf); err != nil {
					t.Fatal(err)
				}
				inflightPayloads -= numPayloadsFittingInSendBuf
				return inflightPayloads
			}()

			// Expect draining N packets lets N more be processed.
			{
				drained, waiter := linkEp.Drain()
				if got, want := drained, uint(numPayloadsFittingInSendBuf); got != want {
					t.Fatalf("got blockingLinkEp.Drain() = %d, want %d", got, want)
				}
				if inflightPayloads < drained {
					t.Fatalf("wrote %d payloads, want at least %d", inflightPayloads, drained)
				}
				<-waiter
				if err := expectLinkEpEnqueued(drained); err != nil {
					t.Fatal(err)
				}
				inflightPayloads -= drained
			}

			validateClose := func() error {
				// Expect the cancel routine is not called before the endpoint was closed.
				if err := ctx.Err(); err != nil {
					return fmt.Errorf("ctx unexpectedly closed with error: %w", err)
				}
				if _, err := s.Close(context.Background()); err != nil {
					return fmt.Errorf("s.Close(): %w", err)
				}

				// Expect the cancel routine is called when the endpoint is closed.
				<-ctx.Done()
				return nil
			}

			if test.closeWhileBlocked {
				if err := validateClose(); err != nil {
					t.Fatal(err)
				}
				// Closing the endpoint while it is blocked drops outgoing payloads
				// on the floor.
				if err := expectLinkEpEnqueued(0); err != nil {
					t.Fatal(err)
				}
			} else {
				linkEp.SetBlocking(false)
				if err := validateClose(); err != nil {
					t.Fatal(err)
				}
				// When the endpoint is unblocked, Close() should block until all
				// remaining payloads are sent.
				if err := expectLinkEpEnqueued(inflightPayloads); err != nil {
					t.Fatal(err)
				}
			}
		})
	}
}
