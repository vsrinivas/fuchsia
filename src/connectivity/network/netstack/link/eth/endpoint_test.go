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

func cycleTX(txFifo zx.Handle, size uint32, iob eth.IOBuffer, fn func([]byte)) error {
	b := make([]eth.FifoEntry, size)
	for toRead := size; toRead != 0; {
		if _, err := zxwait.Wait(txFifo, zx.SignalFIFOReadable, zx.TimensecInfinite); err != nil {
			return err
		}
		status, read := eth.FifoRead(txFifo, b)
		if status != zx.ErrOk {
			return &zx.Error{Status: status, Text: "FifoRead"}
		}
		for _, entry := range b[:read] {
			if fn != nil {
				fn(iob.BufferFromEntry(entry))
			}
		}
		toRead -= read
		status, wrote := eth.FifoWrite(txFifo, b[:read])
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
	const maxDepth = eth.FifoMaxSize / uint(unsafe.Sizeof(eth.FifoEntry{}))

	for i := 0; i < bits.Len(maxDepth); i++ {
		depth := uint32(1 << i)
		t.Run(fmt.Sprintf("depth=%d", depth), func(t *testing.T) {
			var clientTxFifo, deviceTxFifo zx.Handle
			if status := zx.Sys_fifo_create(uint(depth), uint(unsafe.Sizeof(eth.FifoEntry{})), 0, &clientTxFifo, &deviceTxFifo); status != zx.ErrOk {
				t.Fatal(status)
			}
			defer func() {
				_ = clientTxFifo.Close()
				_ = deviceTxFifo.Close()
			}()
			var clientRxFifo, deviceRxFifo zx.Handle
			if status := zx.Sys_fifo_create(uint(depth), uint(unsafe.Sizeof(eth.FifoEntry{})), 0, &clientRxFifo, &deviceRxFifo); status != zx.ErrOk {
				t.Fatal(status)
			}
			defer func() {
				_ = clientRxFifo.Close()
				_ = deviceRxFifo.Close()
			}()

			var device struct {
				iob       eth.IOBuffer
				rxEntries []eth.FifoEntry
			}
			defer func() {
				_ = device.iob.Close()
			}()

			client, err := eth.NewClient(t.Name(), "topo", "file", &ethernetext.Device{
				TB: t,
				GetInfoImpl: func() (ethernet.Info, error) {
					return ethernet.Info{}, nil
				},
				GetFifosImpl: func() (int32, *ethernet.Fifos, error) {
					return int32(zx.ErrOk), &ethernet.Fifos{
						Rx:      clientRxFifo,
						RxDepth: depth,
						Tx:      clientTxFifo,
						TxDepth: depth,
					}, nil
				},
				SetIoBufferImpl: func(vmo zx.VMO) (int32, error) {
					iob, err := eth.MakeIOBuffer(vmo)
					if err != nil {
						t.Fatal(err)
					}
					device.iob = iob
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
			})
			if err != nil {
				t.Fatal(err)
			}
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
				b := make([]eth.FifoEntry, depth+1)
				if _, err := zxwait.Wait(deviceRxFifo, zx.SignalFIFOReadable, zx.TimensecInfinite); err != nil {
					t.Fatal(err)
				}
				status, count := eth.FifoRead(deviceRxFifo, b)
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
						wantRxReads := fifoReadsTransformer(client.Stats.Rx)
						wantTxWrites := fifoWritesTransformer(client.Stats.Tx)

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
						pkts := make([]tcpip.PacketBuffer, writeSize)
						for i := range pkts {
							pkts[i] = tcpip.PacketBuffer{
								Header: buffer.NewPrependable(int(endpoint.MaxHeaderLength())),
							}
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
							if _, err := zxwait.Wait(deviceRxFifo, zx.SignalFIFOWritable, zx.TimensecInfinite); err != nil {
								t.Fatal(err)
							}
							status, count := eth.FifoWrite(deviceRxFifo, b)
							if status != zx.ErrOk {
								t.Fatal(status)
							}
							// The maximum number of RX entries we might be holding is equal to the FIFO depth;
							// we should always be able to write all of them.
							if l := uint32(len(b)); count != l {
								t.Fatalf("got eth.FifoWrite(...) = %d, want = %d", count, l)
							}
							toWrite -= count
							for len(b) != 0 {
								if _, err := zxwait.Wait(deviceRxFifo, zx.SignalFIFOReadable, zx.TimensecInfinite); err != nil {
									t.Fatal(err)
								}
								status, count := eth.FifoRead(deviceRxFifo, b)
								if status != zx.ErrOk {
									t.Fatal(status)
								}
								b = b[count:]
							}
						}

						// NB: only assert on the reads, since RX writes are unsynchronized wrt this test.
						if diff := cmp.Diff(wantRxReads, fifoReadsTransformer(client.Stats.Rx)); diff != "" {
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
						if got, want := count, len(pkts); got != want {
							t.Fatalf("got WritePackets(_) = %d, nil, want %d, nil", got, want)
						}

						if err := cycleTX(deviceTxFifo, writeSize, device.iob, nil); err != nil {
							t.Fatal(err)
						}
						if err := checkTXDone(deviceTxFifo); err != nil {
							t.Fatal(err)
						}

						// NB: only assert on the writes, since TX reads are unsynchronized wrt this test.
						if diff := cmp.Diff(wantTxWrites, fifoWritesTransformer(client.Stats.Tx)); diff != "" {
							t.Errorf("Stats.Tx.Writes mismatch (-want +got):\n%s", diff)
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
			hdr := buffer.NewPrependable(int(endpoint.MaxHeaderLength()) + len(packetHeader) + 5)
			if got, want := copy(hdr.Prepend(len(packetHeader)), packetHeader), len(packetHeader); got != want {
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
			want := DeliverNetworkPacketArgs{
				SrcLinkAddr: localLinkAddress,
				DstLinkAddr: remoteLinkAddress,
				Protocol:    protocol,
				Pkt: tcpip.PacketBuffer{
					Data: buffer.View(packetHeader + body).ToVectorisedView(),
				},
			}

			t.Run("WritePacket", func(t *testing.T) {
				for i := 0; i < int(depth)*10; i++ {
					if err := endpoint.WritePacket(&route, nil, protocol, pb); err != nil {
						t.Fatal(err)
					}

					if err := cycleTX(deviceTxFifo, 1, device.iob, func(b []byte) {
						if len(b) < header.EthernetMinimumSize {
							t.Fatalf("got len(b) = %d, want >= %d", len(b), header.EthernetMinimumSize)
						}
						h := header.Ethernet(b)
						if diff := cmp.Diff(want, DeliverNetworkPacketArgs{
							SrcLinkAddr: h.SourceAddress(),
							DstLinkAddr: h.DestinationAddress(),
							Protocol:    h.Type(),
							Pkt: tcpip.PacketBuffer{
								Data: buffer.View(b[header.EthernetMinimumSize:]).ToVectorisedView(),
							},
						}, cmp.Comparer(vectorizedViewComparer), cmp.Comparer(prependableComparer)); diff != "" {
							t.Fatalf("delivered network packet mismatch (-want +got):\n%s", diff)
						}
					}); err != nil {
						t.Fatal(err)
					}
				}
				if err := checkTXDone(deviceTxFifo); err != nil {
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
						status, count := eth.FifoWrite(deviceRxFifo, device.rxEntries[:1])
						if status != zx.ErrOk {
							t.Fatal(status)
						}
						if count != 1 {
							t.Fatalf("got zx_fifo_write(...) = %d want = %d", count, 1)
						}
					}
					if _, err := zxwait.Wait(deviceRxFifo, zx.SignalFIFOReadable, zx.TimensecInfinite); err != nil {
						t.Fatal(err)
					}
					// Assert that we read back only one entry (when depth is greater than 1).
					status, count := eth.FifoRead(deviceRxFifo, device.rxEntries)
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
							Pkt: tcpip.PacketBuffer{
								Data: buffer.View(payload[:extra]).ToVectorisedView(),
							},
						}, args, cmp.Comparer(vectorizedViewComparer), cmp.Comparer(prependableComparer)); diff != "" {
							t.Fatalf("delivered network packet mismatch (-want +got):\n%s", diff)
						}
					}
				}
			})
		})
	}
}
