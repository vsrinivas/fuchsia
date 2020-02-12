// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package eth_test

import (
	"bytes"
	"fmt"
	"math/bits"
	"syscall/zx"
	"syscall/zx/zxwait"
	"testing"
	"time"
	"unsafe"

	"netstack/link/eth"

	"fidl/fuchsia/hardware/ethernet"
	ethernetext "fidlext/fuchsia/hardware/ethernet"

	"github.com/google/go-cmp/cmp"
	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/buffer"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

type DeliverNetworkPacketArgs struct {
	SrcLinkAddr, DstLinkAddr tcpip.LinkAddress
	Protocol                 tcpip.NetworkProtocolNumber
	Pkt                      tcpip.PacketBuffer
}

type dispatcherChan chan DeliverNetworkPacketArgs

var _ stack.NetworkDispatcher = (*dispatcherChan)(nil)

func (ch *dispatcherChan) DeliverNetworkPacket(_ stack.LinkEndpoint, srcLinkAddr, dstLinkAddr tcpip.LinkAddress, protocol tcpip.NetworkProtocolNumber, pkt tcpip.PacketBuffer) {
	*ch <- DeliverNetworkPacketArgs{
		SrcLinkAddr: srcLinkAddr,
		DstLinkAddr: dstLinkAddr,
		Protocol:    protocol,
		Pkt:         pkt,
	}
}

func vectorizedViewComparer(x, y buffer.VectorisedView) bool {
	return bytes.Equal(x.ToView(), y.ToView())
}

func prependableComparer(x, y buffer.Prependable) bool {
	return bytes.Equal(x.View(), y.View())
}

func fifoReadsTransformer(in eth.FifoStats) []uint64 {
	reads := make([]uint64, in.Size())
	for i := in.Size(); i > 0; i-- {
		reads[i-1] = in.Reads(i).Value()
	}
	return reads
}

func fifoWritesTransformer(in eth.FifoStats) []uint64 {
	writes := make([]uint64, in.Size())
	for i := in.Size(); i > 0; i-- {
		writes[i-1] = in.Writes(i).Value()
	}
	return writes
}

func TestEndpoint(t *testing.T) {
	const maxDepth = eth.FifoMaxSize / uint(unsafe.Sizeof(eth.FifoEntry{}))

	arena, err := eth.NewArena()
	if err != nil {
		t.Fatal(err)
	}

	for i := 0; i < bits.Len(maxDepth); i++ {
		depth := uint32(1 << i)
		t.Run(fmt.Sprintf("depth=%d", depth), func(t *testing.T) {
			var inRxFifo, outTxFifo zx.Handle
			if status := zx.Sys_fifo_create(uint(depth), uint(unsafe.Sizeof(eth.FifoEntry{})), 0, &inRxFifo, &outTxFifo); status != zx.ErrOk {
				t.Fatal(status)
			}
			defer func() {
				_ = inRxFifo.Close()
				_ = outTxFifo.Close()
			}()
			var inTxFifo, outRxFifo zx.Handle
			if status := zx.Sys_fifo_create(uint(depth), uint(unsafe.Sizeof(eth.FifoEntry{})), 0, &inTxFifo, &outRxFifo); status != zx.ErrOk {
				t.Fatal(status)
			}
			defer func() {
				_ = inTxFifo.Close()
				_ = outRxFifo.Close()
			}()

			baseDevice := ethernetext.Device{
				TB: t,
				GetInfoImpl: func() (ethernet.Info, error) {
					return ethernet.Info{}, nil
				},
				SetIoBufferImpl: func(zx.VMO) (int32, error) {
					return int32(zx.ErrOk), nil
				},
				StartImpl: func() (int32, error) {
					return int32(zx.ErrOk), nil
				},
				StopImpl: func() error {
					return nil
				},
				SetClientNameImpl: func(string) (int32, error) {
					return int32(zx.ErrOk), nil
				},
				ConfigMulticastSetPromiscuousModeImpl: func(bool) (int32, error) {
					return int32(zx.ErrOk), nil
				},
			}

			inDevice := baseDevice
			inDevice.GetFifosImpl = func() (int32, *ethernet.Fifos, error) {
				return int32(zx.ErrOk), &ethernet.Fifos{
					Rx:      inRxFifo,
					RxDepth: depth,
					Tx:      inTxFifo,
					TxDepth: depth,
				}, nil
			}
			inClient, err := eth.NewClient(t.Name(), "inTopo", "inFile", &inDevice, arena)
			if err != nil {
				t.Fatal(err)
			}
			defer func() {
				_ = inClient.Close()
				inClient.Wait()
			}()
			inCh := make(dispatcherChan, depth)
			defer close(inCh)
			{
				inEndpoint := eth.NewLinkEndpoint(inClient)
				inEndpoint.Attach(&inCh)
			}
			// Both sides are trying to "seed" the RX direction, which is not valid. Drain the buffers sent
			// by the "in" client and discard them.
			if _, err := zxwait.Wait(outTxFifo, zx.SignalFIFOReadable, zx.TimensecInfinite); err != nil {
				t.Fatal(err)
			}
			{
				b := make([]eth.FifoEntry, depth+1)
				status, count := eth.FifoRead(outTxFifo, b)
				if status != zx.ErrOk {
					t.Fatal(status)
				}
				if count != depth {
					t.Fatalf("got zx_fifo_read(...) = %d want = %d", count, depth)
				}
			}

			outDevice := baseDevice
			outDevice.GetFifosImpl = func() (int32, *ethernet.Fifos, error) {
				return int32(zx.ErrOk), &ethernet.Fifos{
					Rx:      outRxFifo,
					RxDepth: depth,
					Tx:      outTxFifo,
					TxDepth: depth,
				}, nil
			}
			outClient, err := eth.NewClient(t.Name(), "outTopo", "outFile", &outDevice, arena)
			if err != nil {
				t.Fatal(err)
			}
			defer func() {
				_ = outClient.Close()
				outClient.Wait()
			}()
			outEndpoint := eth.NewLinkEndpoint(outClient)
			{
				var outCh dispatcherChan
				outEndpoint.Attach(&outCh)
			}

			t.Run("Stats", func(t *testing.T) {
				for excess := depth; ; excess >>= 1 {
					t.Run(fmt.Sprintf("excess=%d", excess), func(t *testing.T) {
						// Grab baseline stats to avoid assumptions about ops done to this point.
						wantTxWrites := fifoWritesTransformer(outClient.Stats.Tx)
						wantRxReads := fifoReadsTransformer(inClient.Stats.Rx)

						// Compute expectations.
						for _, want := range [][]uint64{wantTxWrites, wantRxReads} {
							for _, write := range []uint32{depth, excess} {
								if write == 0 {
									continue
								}
								want[write-1]++
							}
						}

						writeSize := depth + excess
						pkts := make([]tcpip.PacketBuffer, writeSize)
						for i := range pkts {
							pkts[i] = tcpip.PacketBuffer{
								Header: buffer.NewPrependable(int(outEndpoint.MaxHeaderLength())),
							}
						}

						// Use WritePackets to get deterministic batch sizes.
						count, err := outEndpoint.WritePackets(
							&stack.Route{},
							nil,
							pkts,
							1337,
						)
						if err != nil {
							t.Fatal(err)
						}
						if got, want := count, int(writeSize); got != want {
							t.Fatalf("got WritePackets(_) = %d, nil, want %d, nil", got, want)
						}

						for i := uint32(0); i < writeSize; i++ {
							select {
							case <-time.After(5 * time.Second):
								t.Fatalf("timeout waiting for %d/%d packets", i, writeSize)
							case <-inCh:
							}
						}

						for i := 0; ; i++ {
							select {
							case <-time.After(10 * time.Millisecond):
							case args := <-inCh:
								t.Errorf("unexpected packet received: %+v", args)
								continue
							}
							break
						}
						if t.Failed() {
							t.FailNow()
						}

						// NB: only assert on the writes, since TX reads are unsynchronized wrt this test.
						if diff := cmp.Diff(wantTxWrites, fifoWritesTransformer(outClient.Stats.Tx)); diff != "" {
							t.Errorf("outClient.Stats.Tx.Writes mismatch (-want +got):\n%s", diff)
						}
						// NB: only assert on the reads, since RX writes are unsynchronized wrt this test.
						if diff := cmp.Diff(wantRxReads, fifoReadsTransformer(inClient.Stats.Rx)); diff != "" {
							t.Errorf("inClient.Stats.Rx.Reads mismatch (-want +got):\n%s", diff)
						}
					})
					if excess == 0 {
						break
					}
				}
			})

			const localLinkAddress = tcpip.LinkAddress("\x01\x02\x03\x04\x05\x06")
			const remoteLinkAddress = tcpip.LinkAddress("\x11\x12\x13\x14\x15\x16")
			const protocol = tcpip.NetworkProtocolNumber(45)

			// Test that we build the ethernet frame correctly.
			// Test that we don't accidentally put unused bytes on the wire.
			const packetHeader = "foo"
			hdr := buffer.NewPrependable(int(outEndpoint.MaxHeaderLength()) + len(packetHeader) + 5)
			if want, got := len(packetHeader), copy(hdr.Prepend(len(packetHeader)), packetHeader); got != want {
				t.Fatalf("got copy() = %d, want = %d", got, want)
			}
			const body = "bar"
			route := stack.Route{
				LocalLinkAddress:  localLinkAddress,
				RemoteLinkAddress: remoteLinkAddress,
			}
			pb := tcpip.PacketBuffer{
				Data:   buffer.View(body).ToVectorisedView(),
				Header: hdr,
			}

			t.Run("WritePacket", func(t *testing.T) {
				for i := 0; i < int(depth)*10; i++ {
					if err := outEndpoint.WritePacket(&route, nil, protocol, pb); err != nil {
						t.Fatal(err)
					}
					select {
					case <-time.After(5 * time.Second):
						t.Fatalf("timeout waiting for ethernet packet on iteration %d", i)
					case args := <-inCh:
						if diff := cmp.Diff(args, DeliverNetworkPacketArgs{
							SrcLinkAddr: localLinkAddress,
							DstLinkAddr: remoteLinkAddress,
							Protocol:    protocol,
							Pkt: tcpip.PacketBuffer{
								Data: buffer.View(packetHeader + body).ToVectorisedView(),
							},
						}, cmp.Comparer(vectorizedViewComparer), cmp.Comparer(prependableComparer)); diff != "" {
							t.Fatalf("delivered network packet mismatch (-want +got):\n%s", diff)
						}
					}
				}
			})

			// ReceivePacket tests that receiving ethernet frames of size
			// less than the minimum size does not panic or cause any issues for future
			// (valid) frames.
			t.Run("ReceivePacket", func(t *testing.T) {
				const payload = "foobarbaz"
				var headerBuffer [header.EthernetMinimumSize]byte
				header.Ethernet(headerBuffer[:]).Encode(&header.EthernetFields{
					SrcAddr: localLinkAddress,
					DstAddr: remoteLinkAddress,
					Type:    protocol,
				})

				// Send the first sendSize bytes of a frame from outClient to inClient.
				send := func(sendSize int) {
					buf := buffer.View(append(headerBuffer[:], payload...))
					if got, want := len(buf), header.EthernetMinimumSize+len(payload); got != want {
						t.Fatalf("got len = %d, want %d", got, want)
					}

					if err := outClient.WriteRawPacket(buf[:sendSize].ToVectorisedView()); err != nil {
						t.Fatal(err)
					}
				}

				// Test receiving a frame that is too small.
				send(header.EthernetMinimumSize - 1)
				select {
				case <-time.After(10 * time.Millisecond):
				case args := <-inCh:
					t.Fatalf("unexpected packet received: %+v", args)
				}

				for _, extra := range []int{
					// Test receiving a frame that is equal to the minimum frame size.
					0,
					// Test receiving a frame that is just greater than the minimum frame size.
					1,
					// Test receiving the full frame.
					len(payload),
				} {
					send(header.EthernetMinimumSize + extra)

					// Wait for a packet to be delivered on ch and validate the delivered
					// network packet parameters. The packet should be delivered within 5s.
					select {
					case <-time.After(5 * time.Second):
						t.Fatal("timeout waiting for ethernet packet")
					case args := <-inCh:
						if diff := cmp.Diff(args, DeliverNetworkPacketArgs{
							SrcLinkAddr: localLinkAddress,
							DstLinkAddr: remoteLinkAddress,
							Protocol:    protocol,
							Pkt: tcpip.PacketBuffer{
								Data: buffer.View(payload[:extra]).ToVectorisedView(),
							},
						}, cmp.Comparer(vectorizedViewComparer), cmp.Comparer(prependableComparer)); diff != "" {
							t.Fatalf("delivered network packet mismatch (-want +got):\n%s", diff)
						}
					}
				}
			})
		})
	}
}
