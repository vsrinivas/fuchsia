// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package eth_test

import (
	"fmt"
	"math/bits"
	"runtime"
	"syscall/zx"
	"syscall/zx/zxwait"
	"testing"
	"time"
	"unsafe"

	eth_gen "gen/netstack/link/eth"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link/eth"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link/fifo"
	fifotestutil "go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link/fifo/testutil"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/testutil"

	"fidl/fuchsia/hardware/ethernet"

	"github.com/google/go-cmp/cmp"
	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/buffer"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

type DeliverNetworkPacketArgs struct {
	SrcLinkAddr, DstLinkAddr tcpip.LinkAddress
	Protocol                 tcpip.NetworkProtocolNumber
	Pkt                      *stack.PacketBuffer
}

type dispatcherChan chan DeliverNetworkPacketArgs

var _ stack.NetworkDispatcher = (*dispatcherChan)(nil)

func (ch *dispatcherChan) DeliverNetworkPacket(srcLinkAddr, dstLinkAddr tcpip.LinkAddress, protocol tcpip.NetworkProtocolNumber, pkt *stack.PacketBuffer) {
	*ch <- DeliverNetworkPacketArgs{
		SrcLinkAddr: srcLinkAddr,
		DstLinkAddr: dstLinkAddr,
		Protocol:    protocol,
		Pkt:         pkt,
	}
}

func (*dispatcherChan) DeliverOutboundPacket(_, _ tcpip.LinkAddress, _ tcpip.NetworkProtocolNumber, _ *stack.PacketBuffer) {
}

func fifoReadsTransformer(in *fifo.FifoStats) []uint64 {
	reads := make([]uint64, in.Size())
	for i := in.Size(); i > 0; i-- {
		reads[i-1] = in.Reads(i).Value()
	}
	return reads
}

func fifoWritesTransformer(in *fifo.FifoStats) []uint64 {
	writes := make([]uint64, in.Size())
	for i := in.Size(); i > 0; i-- {
		writes[i-1] = in.Writes(i).Value()
	}
	return writes
}

func cycleTX(txFifo zx.Handle, size uint32, iob eth.IOBuffer, fn func([]byte)) error {
	b := make([]eth_gen.FifoEntry, size)
	for toRead := size; toRead != 0; {
		if _, err := zxwait.Wait(txFifo, zx.SignalFIFOReadable, zx.TimensecInfinite); err != nil {
			return err
		}
		status, read := eth_gen.FifoRead(txFifo, b)
		if status != zx.ErrOk {
			return &zx.Error{Status: status, Text: "FifoRead"}
		}
		for _, entry := range b[:read] {
			if fn != nil {
				fn(iob.BufferFromEntry(entry))
			}
		}
		toRead -= read
		status, wrote := eth_gen.FifoWrite(txFifo, b[:read])
		if status != zx.ErrOk {
			return &zx.Error{Status: status, Text: "FifoWrite"}
		}
		if wrote != read {
			return fmt.Errorf("got zx_fifo_write(...) = %d want = %d", wrote, size)
		}
	}
	return nil
}

func checkTXDone(txFifo zx.Handle) error {
	_, err := zxwait.Wait(txFifo, zx.SignalFIFOReadable, zx.Sys_deadline_after(zx.Duration(10*time.Millisecond.Nanoseconds())))
	if err, ok := err.(*zx.Error); ok && err.Status == zx.ErrTimedOut {
		return nil
	}
	return fmt.Errorf("got zxwait.Wait(txFifo, ...) = %v, want %s", err, zx.ErrTimedOut)
}

func TestEndpoint(t *testing.T) {
	const maxDepth = eth_gen.FifoMaxSize / uint(unsafe.Sizeof(eth_gen.FifoEntry{}))

	for i := 0; i < bits.Len(maxDepth); i++ {
		depth := uint32(1 << i)
		t.Run(fmt.Sprintf("depth=%d", depth), func(t *testing.T) {
			deviceImpl, deviceFifos := fifotestutil.MakeEthernetDevice(t, ethernet.Info{}, depth)

			var device struct {
				iob       eth.IOBuffer
				rxEntries []eth_gen.FifoEntry
			}
			defer func() {
				_ = device.iob.Close()
			}()

			deviceImpl.SetIoBufferImpl = func(vmo zx.VMO) (int32, error) {
				mappedVmo, err := fifo.MapVMO(vmo)
				if err != nil {
					t.Fatal(err)
				}
				if err := vmo.Close(); err != nil {
					t.Fatal(err)
				}
				device.iob = eth.IOBuffer{MappedVMO: mappedVmo}
				return int32(zx.ErrOk), nil
			}

			client, err := eth.NewClient(t.Name(), "topo", "file", &deviceImpl)
			if err != nil {
				t.Fatal(err)
			}
			client.SetOnLinkClosed(func() {})
			defer func() {
				_ = client.Close()
				client.Wait()
			}()

			if device.iob == (eth.IOBuffer{}) {
				t.Fatal("eth.NewClient didn't call device.SetIoBuffer")
			}

			endpoint := eth.NewLinkEndpoint(client)
			ch := make(dispatcherChan, 1)
			endpoint.Attach(&ch)

			// Attaching a dispatcher to the client should cause it to fill the device's RX buffer pool.
			{
				b := make([]eth_gen.FifoEntry, depth+1)
				if _, err := zxwait.Wait(deviceFifos.Rx, zx.SignalFIFOReadable, zx.TimensecInfinite); err != nil {
					t.Fatal(err)
				}
				status, count := eth_gen.FifoRead(deviceFifos.Rx, b)
				if status != zx.ErrOk {
					t.Fatal(status)
				}
				if count != depth {
					t.Fatalf("got zx_fifo_read(...) = %d want = %d", count, depth)
				}
				device.rxEntries = append(device.rxEntries, b[:count]...)
			}

			t.Run("Stats", func(t *testing.T) {
				for excess := depth; ; excess >>= 1 {
					t.Run(fmt.Sprintf("excess=%d", excess), func(t *testing.T) {
						// Grab baseline stats to avoid assumptions about ops done to this point.
						wantRxReads := fifoReadsTransformer(&client.RxStats().FifoStats)
						wantTxWrites := fifoWritesTransformer(&client.TxStats().FifoStats)

						// Compute expectations.
						for _, want := range [][]uint64{wantRxReads, wantTxWrites} {
							for _, write := range []uint32{depth, excess} {
								if write == 0 {
									continue
								}
								want[write-1]++
							}
						}

						writeSize := depth + excess
						var pkts stack.PacketBufferList
						for i := uint32(0); i < writeSize; i++ {
							pkts.PushBack(stack.NewPacketBuffer(stack.PacketBufferOptions{
								ReserveHeaderBytes: int(endpoint.MaxHeaderLength()),
							}))
						}

						// Simulate zero-sized incoming packets; zero-sized packets will increment fifo stats
						// without dispatching up the stack.
						for toWrite := writeSize; toWrite != 0; {
							b := device.rxEntries
							if toWrite < uint32(len(b)) {
								b = b[:toWrite]
							}
							for i := range b {
								b[i].SetLength(0)
							}
							if _, err := zxwait.Wait(deviceFifos.Rx, zx.SignalFIFOWritable, zx.TimensecInfinite); err != nil {
								t.Fatal(err)
							}
							status, count := eth_gen.FifoWrite(deviceFifos.Rx, b)
							if status != zx.ErrOk {
								t.Fatal(status)
							}
							// The maximum number of RX entries we might be holding is equal to the FIFO depth;
							// we should always be able to write all of them.
							if l := uint32(len(b)); count != l {
								t.Fatalf("got eth_gen.FifoWrite(...) = %d, want = %d", count, l)
							}
							toWrite -= count
							for len(b) != 0 {
								if _, err := zxwait.Wait(deviceFifos.Rx, zx.SignalFIFOReadable, zx.TimensecInfinite); err != nil {
									t.Fatal(err)
								}
								status, count := eth_gen.FifoRead(deviceFifos.Rx, b)
								if status != zx.ErrOk {
									t.Fatal(status)
								}
								b = b[count:]
							}
						}

						// NB: only assert on the reads, since RX writes are unsynchronized wrt this test.
						if diff := cmp.Diff(wantRxReads, fifoReadsTransformer(&client.RxStats().FifoStats)); diff != "" {
							t.Errorf("Stats.Rx.Reads mismatch (-want +got):\n%s", diff)
						}

						// Use WritePackets to get deterministic batch sizes.
						count, err := endpoint.WritePackets(
							&stack.Route{},
							nil,
							pkts,
							1337,
						)
						if err != nil {
							t.Fatal(err)
						}
						if got := uint32(count); got != writeSize {
							t.Fatalf("got WritePackets(_) = %d, nil, want %d, nil", got, writeSize)
						}

						dropsBefore := client.TxStats().Drops.Value()

						if err := cycleTX(deviceFifos.Tx, writeSize, device.iob, nil); err != nil {
							t.Fatal(err)
						}
						if err := checkTXDone(deviceFifos.Tx); err != nil {
							t.Fatal(err)
						}

						// NB: only assert on the writes, since TX read FIFO stats are
						// unsynchronized wrt this test. Even though we poll for drop stats,
						// we can't make guarantees for the FIFO stats.
						if diff := cmp.Diff(wantTxWrites, fifoWritesTransformer(&client.TxStats().FifoStats)); diff != "" {
							t.Errorf("Stats.Tx.Writes mismatch (-want +got):\n%s", diff)
						}

						// Since we're not setting the TX_OK flag in this test, all these
						// packets will be considered to have dropped. We need to poll until
						// the stats match expectations since we're unsynchronized on TX
						// reads, which is when the value increments.
						for {
							dropsAfter := client.TxStats().Drops.Value() - dropsBefore
							if dropsAfter < uint64(writeSize) {
								runtime.Gosched()
								continue
							}
							if dropsAfter != uint64(writeSize) {
								t.Errorf("got client.TxStates.Drops.Value() = %d, want %d", dropsAfter, writeSize)
							}
							break
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
			const body = "bar"
			route := stack.Route{
				LocalLinkAddress:  localLinkAddress,
				RemoteLinkAddress: remoteLinkAddress,
			}
			want := DeliverNetworkPacketArgs{
				SrcLinkAddr: localLinkAddress,
				DstLinkAddr: remoteLinkAddress,
				Protocol:    protocol,
				Pkt: &stack.PacketBuffer{
					Data: buffer.View(packetHeader + body).ToVectorisedView(),
				},
			}

			data := buffer.View(body).ToVectorisedView()

			t.Run("WritePacket", func(t *testing.T) {
				for i := 0; i < int(depth)*10; i++ {
					pkt := stack.NewPacketBuffer(stack.PacketBufferOptions{
						ReserveHeaderBytes: int(endpoint.MaxHeaderLength()) + len(packetHeader) + 5,
						Data:               data,
					})
					hdr := pkt.NetworkHeader().Push(len(packetHeader))
					if n := copy(hdr, packetHeader); n != len(packetHeader) {
						t.Fatalf("copied %d bytes, expected %d bytes", n, len(packetHeader))
					}
					if err := endpoint.WritePacket(&route, nil, protocol, pkt); err != nil {
						t.Fatal(err)
					}

					if err := cycleTX(deviceFifos.Tx, 1, device.iob, func(b []byte) {
						if len(b) < header.EthernetMinimumSize {
							t.Fatalf("got len(b) = %d, want >= %d", len(b), header.EthernetMinimumSize)
						}
						h := header.Ethernet(b)
						if diff := cmp.Diff(want, DeliverNetworkPacketArgs{
							SrcLinkAddr: h.SourceAddress(),
							DstLinkAddr: h.DestinationAddress(),
							Protocol:    h.Type(),
							Pkt: &stack.PacketBuffer{
								Data: buffer.View(b[header.EthernetMinimumSize:]).ToVectorisedView(),
							},
						}, testutil.PacketBufferCmpTransformer); diff != "" {
							t.Fatalf("delivered network packet mismatch (-want +got):\n%s", diff)
						}
					}); err != nil {
						t.Fatal(err)
					}
				}
				if err := checkTXDone(deviceFifos.Tx); err != nil {
					t.Fatal(err)
				}
			})

			// ReceivePacket tests that receiving ethernet frames of size
			// less than the minimum size does not panic or cause any issues for future
			// (valid) frames.
			t.Run("ReceivePacket", func(t *testing.T) {
				const payload = "foobarbaz"

				// Send the first sendSize bytes of a frame.
				send := func(sendSize int) {
					entry := &device.rxEntries[0]
					buf := device.iob.BufferFromEntry(*entry)
					header.Ethernet(buf).Encode(&header.EthernetFields{
						SrcAddr: localLinkAddress,
						DstAddr: remoteLinkAddress,
						Type:    protocol,
					})
					if got, want := copy(buf[header.EthernetMinimumSize:], payload), len(payload); got != want {
						t.Fatalf("got copy() = %d, want %d", got, want)
					}
					entry.SetLength(sendSize)

					{
						status, count := eth_gen.FifoWrite(deviceFifos.Rx, device.rxEntries[:1])
						if status != zx.ErrOk {
							t.Fatal(status)
						}
						if count != 1 {
							t.Fatalf("got zx_fifo_write(...) = %d want = %d", count, 1)
						}
					}
					if _, err := zxwait.Wait(deviceFifos.Rx, zx.SignalFIFOReadable, zx.TimensecInfinite); err != nil {
						t.Fatal(err)
					}
					// Assert that we read back only one entry (when depth is greater than 1).
					status, count := eth_gen.FifoRead(deviceFifos.Rx, device.rxEntries)
					if status != zx.ErrOk {
						t.Fatal(status)
					}
					if count != 1 {
						t.Fatalf("got zx_fifo_write(...) = %d want = %d", count, 1)
					}
				}

				// Test receiving a frame that is too small.
				send(header.EthernetMinimumSize - 1)
				select {
				case <-time.After(10 * time.Millisecond):
				case args := <-ch:
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
					case args := <-ch:
						if diff := cmp.Diff(DeliverNetworkPacketArgs{
							SrcLinkAddr: localLinkAddress,
							DstLinkAddr: remoteLinkAddress,
							Protocol:    protocol,
							Pkt: &stack.PacketBuffer{
								Data: buffer.View(payload[:extra]).ToVectorisedView(),
							},
						}, args, testutil.PacketBufferCmpTransformer); diff != "" {
							t.Fatalf("delivered network packet mismatch (-want +got):\n%s", diff)
						}
					}
				}
			})
		})
	}
}
