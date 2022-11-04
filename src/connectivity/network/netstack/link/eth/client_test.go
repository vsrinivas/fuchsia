// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package eth_test

import (
	"context"
	"errors"
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
	fifotestutil "go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link/eth/testutil"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link/fifo"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/testutil"

	fidlethernet "fidl/fuchsia/hardware/ethernet"
	"fidl/fuchsia/hardware/network"

	"github.com/google/go-cmp/cmp"
	"gvisor.dev/gvisor/pkg/bufferv2"
	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

type DeliverNetworkPacketArgs struct {
	Protocol tcpip.NetworkProtocolNumber
	Pkt      stack.PacketBufferPtr
}

type dispatcherChan chan DeliverNetworkPacketArgs

var _ stack.NetworkDispatcher = (dispatcherChan)(nil)

func (ch dispatcherChan) DeliverNetworkPacket(protocol tcpip.NetworkProtocolNumber, pkt stack.PacketBufferPtr) {
	pkt.IncRef()

	ch <- DeliverNetworkPacketArgs{
		Protocol: protocol,
		Pkt:      pkt,
	}
}

func (dispatcherChan) DeliverLinkPacket(tcpip.NetworkProtocolNumber, stack.PacketBufferPtr, bool) {
	panic("not implemented")
}

func (ch dispatcherChan) release() {
	close(ch)
	for args := range ch {
		args.Pkt.DecRef()
	}
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

func uint64Sum(stats []uint64) uint64 {
	var t uint64
	for _, v := range stats {
		t += v
	}
	return t
}

func cycleTX(txFifo zx.Handle, size uint32, iob eth.IOBuffer, fn func([]byte)) error {
	b := make([]eth_gen.FifoEntry, size)
	for toRead := size; toRead != 0; {
		if _, err := zxwait.WaitContext(context.Background(), txFifo, zx.SignalFIFOReadable); err != nil {
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
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Millisecond)
	defer cancel()
	if _, err := zxwait.WaitContext(ctx, txFifo, zx.SignalFIFOReadable); !errors.Is(err, context.DeadlineExceeded) {
		return fmt.Errorf("got zxwait.WaitContext(txFifo, ...) = %v, want %s", err, zx.ErrTimedOut)
	}
	return nil
}

func TestClient(t *testing.T) {
	const maxDepth = eth_gen.FifoMaxSize / uint(unsafe.Sizeof(eth_gen.FifoEntry{}))

	for i := 0; i < bits.Len(maxDepth); i++ {
		depth := uint32(1 << i)
		t.Run(fmt.Sprintf("depth=%d", depth), func(t *testing.T) {
			deviceImpl, deviceFifos := fifotestutil.MakeEthernetDevice(t, fidlethernet.Info{}, depth)

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

			ch := make(dispatcherChan, 1)
			defer ch.release()
			client.Attach(&ch)

			// Attaching a dispatcher to the client should cause it to fill the device's RX buffer pool.
			{
				b := make([]eth_gen.FifoEntry, depth+1)
				if _, err := zxwait.WaitContext(context.Background(), deviceFifos.Rx, zx.SignalFIFOReadable); err != nil {
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

			// Wait until the initial batch of Rx writes is accounted for.
			for uint64Sum(fifoWritesTransformer(&client.RxStats().FifoStats)) == 0 {
				runtime.Gosched()
			}

			t.Run("Stats", func(t *testing.T) {
				for excess := depth; ; excess >>= 1 {
					t.Run(fmt.Sprintf("excess=%d", excess), func(t *testing.T) {
						// Grab baseline stats to avoid assumptions about ops done to this point.
						wantRxReads := fifoReadsTransformer(&client.RxStats().FifoStats)
						wantRxWrites := fifoWritesTransformer(&client.RxStats().FifoStats)
						wantTxReads := fifoReadsTransformer(&client.TxStats().FifoStats)
						wantTxWrites := fifoWritesTransformer(&client.TxStats().FifoStats)

						// Compute expectations.
						for _, want := range [][]uint64{wantRxReads, wantRxWrites, wantTxReads, wantTxWrites} {
							for _, write := range []uint32{depth, excess} {
								if write == 0 {
									continue
								}
								want[write-1]++
							}
						}

						writeSize := depth + excess
						var pkts stack.PacketBufferList
						defer pkts.DecRef()
						for i := uint32(0); i < writeSize; i++ {
							pkts.PushBack(stack.NewPacketBuffer(stack.PacketBufferOptions{
								ReserveHeaderBytes: int(client.MaxHeaderLength()),
							}))
						}

						// Simulate zero-sized incoming packets.
						for toWrite := writeSize; toWrite != 0; {
							b := device.rxEntries
							if toWrite < uint32(len(b)) {
								b = b[:toWrite]
							}
							for i := range b {
								b[i].SetLength(0)
							}
							if _, err := zxwait.WaitContext(context.Background(), deviceFifos.Rx, zx.SignalFIFOWritable); err != nil {
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

							timeout := make(chan struct{})
							time.AfterFunc(5*time.Second, func() { close(timeout) })
							for i := uint32(0); i < count; i++ {
								select {
								case <-timeout:
									t.Fatal("timeout waiting for ethernet packet")
								case args := <-ch:
									func() {
										defer args.Pkt.DecRef()

										pkt := stack.NewPacketBuffer(stack.PacketBufferOptions{})
										defer pkt.DecRef()

										if diff := cmp.Diff(DeliverNetworkPacketArgs{
											Pkt: pkt,
										}, args, testutil.PacketBufferCmpTransformer); diff != "" {
											t.Fatalf("delivered network packet mismatch (-want +got):\n%s", diff)
										}
									}()
								}
							}
							for len(b) != 0 {
								if _, err := zxwait.WaitContext(context.Background(), deviceFifos.Rx, zx.SignalFIFOReadable); err != nil {
									t.Fatal(err)
								}
								status, count := eth_gen.FifoRead(deviceFifos.Rx, b)
								if status != zx.ErrOk {
									t.Fatal(status)
								}
								b = b[count:]
							}
						}

						if diff := cmp.Diff(wantRxReads, fifoReadsTransformer(&client.RxStats().FifoStats)); diff != "" {
							t.Errorf("Stats.Rx.Reads mismatch (-want +got):\n%s", diff)
						}
						// RX write stats must be polled since we're unsynchronized on RX
						// reads; the client will only update the stats after returning the
						// buffers on the FIFO. We poll based on total counts and later
						// assert on the correct per-batch values.
						wantRxWritesTotal := uint64Sum(wantRxWrites)
						for {
							gotRxWrites := fifoWritesTransformer(&client.RxStats().FifoStats)
							if uint64Sum(gotRxWrites) < wantRxWritesTotal {
								runtime.Gosched()
								continue
							}
							if diff := cmp.Diff(wantRxWrites, gotRxWrites); diff != "" {
								t.Errorf("Stats.Rx.Writes mismatch (-want +got):\n%s", diff)
							}
							break
						}

						dropsBefore := client.TxStats().Drops.Value()

						// cycleTX in a goroutine since WritePackets will block until all
						// packets are written and if we send more packets than the TX
						// depth, we will block forever unless something is reading from the
						// TX FIFO as we write.
						cycleTXErr := make(chan error)
						go func() {
							cycleTXErr <- cycleTX(deviceFifos.Tx, writeSize, device.iob, nil)
						}()

						// Use WritePackets to get deterministic batch sizes.
						count, err := client.WritePackets(pkts)
						if err != nil {
							t.Fatal(err)
						}
						if got := uint32(count); got != writeSize {
							t.Fatalf("got WritePackets(_) = %d, want %d", got, writeSize)
						}

						if err := <-cycleTXErr; err != nil {
							t.Fatal(err)
						}
						if err := checkTXDone(deviceFifos.Tx); err != nil {
							t.Fatal(err)
						}

						if diff := cmp.Diff(wantTxWrites, fifoWritesTransformer(&client.TxStats().FifoStats)); diff != "" {
							t.Errorf("Stats.Tx.Writes mismatch (-want +got):\n%s", diff)
						}
						// TX read stats must be polled since we're unsynchronized on TX
						// reads, which is when the stats are updated. We poll based on
						// total counts and later assert on the correct per-batch values.
						wantTxReadsTotal := uint64Sum(wantTxReads)
						for {
							gotTxReads := fifoReadsTransformer(&client.TxStats().FifoStats)
							if uint64Sum(gotTxReads) < wantTxReadsTotal {
								runtime.Gosched()
								continue
							}
							if diff := cmp.Diff(wantTxReads, gotTxReads); diff != "" {
								t.Errorf("Stats.Tx.Reads mismatch (-want +got):\n%s", diff)
							}
							break
						}

						// Since we're not setting the TX_OK flag in this test, all these
						// packets will be considered to have dropped. We similarly need to
						// poll the dropped stats since they're unsynchronized.
						for {
							dropsAfter := client.TxStats().Drops.Value() - dropsBefore
							if dropsAfter < uint64(writeSize) {
								runtime.Gosched()
								continue
							}
							if dropsAfter != uint64(writeSize) {
								t.Errorf("got client.TxStats.Drops.Value() = %d, want %d", dropsAfter, writeSize)
							}
							break
						}
					})
					if excess == 0 {
						break
					}
				}
			})

			// Test that we build the ethernet frame correctly.
			// Test that we don't accidentally put unused bytes on the wire.
			const packetHeader = "foo"
			const body = "bar"

			want := []byte(packetHeader + body)

			t.Run("WritePacket", func(t *testing.T) {
				for i := 0; i < int(depth)*10; i++ {
					func() {
						var pkts stack.PacketBufferList
						defer pkts.DecRef()

						pkt := stack.NewPacketBuffer(stack.PacketBufferOptions{
							ReserveHeaderBytes: int(client.MaxHeaderLength()) + len(packetHeader) + 5,
							Payload:            bufferv2.MakeWithData([]byte(body)),
						})
						pkts.PushBack(pkt)

						if n := copy(pkt.NetworkHeader().Push(len(packetHeader)), packetHeader); n != len(packetHeader) {
							t.Fatalf("got copy(...) = %d, want = %d", n, len(packetHeader))
						}

						if n, err := client.WritePackets(pkts); err != nil {
							t.Fatalf("WritePackets(_): %s", err)
						} else if n != 1 {
							t.Fatalf("got WritePackets(_) = %d, want = 1", n)
						}
					}()

					if err := cycleTX(deviceFifos.Tx, 1, device.iob, func(b []byte) {
						if diff := cmp.Diff(want, b); diff != "" {
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

			send := func(payload string) {
				entry := &device.rxEntries[0]
				buf := device.iob.BufferFromEntry(*entry)
				if got, want := copy(buf, payload), len(payload); got != want {
					t.Fatalf("got copy() = %d, want %d", got, want)
				}
				entry.SetLength(uint16(len(payload)))

				{
					status, count := eth_gen.FifoWrite(deviceFifos.Rx, device.rxEntries[:1])
					if status != zx.ErrOk {
						t.Fatal(status)
					}
					if count != 1 {
						t.Fatalf("got zx_fifo_write(...) = %d want = %d", count, 1)
					}
				}
				if _, err := zxwait.WaitContext(context.Background(), deviceFifos.Rx, zx.SignalFIFOReadable); err != nil {
					t.Fatal(err)
				}
				// Assert that we read back only one entry (when depth is greater than 1).
				status, count := eth_gen.FifoRead(deviceFifos.Rx, device.rxEntries)
				if status != zx.ErrOk {
					t.Fatal(status)
				}
				if count != 1 {
					t.Fatalf("got zx_fifo_read(...) = %d want = %d", count, 1)
				}
			}

			// ReceivePacket tests that receiving ethernet frames of size
			// less than the minimum size does not panic or cause any issues for future
			// (valid) frames.
			t.Run("ReceivePacket", func(t *testing.T) {
				for _, payload := range []string{
					// Test receiving a frame.
					"foobarbaz",
					// Test receiving a frame that is equal to the minimum frame size.
					"",
					// Test receiving a frame that is just greater than the minimum frame size.
					"a",
				} {
					send(payload)

					// Wait for a packet to be delivered on ch and validate the delivered
					// network packet parameters. The packet should be delivered within 5s.
					select {
					case <-time.After(5 * time.Second):
						t.Fatal("timeout waiting for ethernet packet")
					case args := <-ch:
						func() {
							defer args.Pkt.DecRef()

							pkt := stack.NewPacketBuffer(stack.PacketBufferOptions{
								Payload: bufferv2.MakeWithData([]byte(payload)),
							})
							defer pkt.DecRef()

							if diff := cmp.Diff(DeliverNetworkPacketArgs{
								Pkt: pkt,
							}, args, testutil.PacketBufferCmpTransformer); diff != "" {
								t.Fatalf("delivered network packet mismatch (-want +got):\n%s", diff)
							}
						}()
					}
				}
			})

			// ReceivePacketNoMemoryLeak tests that receiving many packets does not
			// leak memory.
			t.Run("ReceivePacketNoMemoryLeak", func(t *testing.T) {
				// Measured in December 2021:
				// * performing 10k runs takes ~900ms.
				// * heap object growth is ranging from negative 50 to 0.
				if err := testutil.CheckHeapObjectsGrowth(10000, 100, func() {
					send("foobarbazfoobar")
					pkt := (<-ch).Pkt
					(&pkt).DecRef()
				}); err != nil {
					t.Fatal(err)
				}
			})
		})
	}
}

func TestDeviceClass(t *testing.T) {
	tests := []struct {
		features    fidlethernet.Features
		expectClass network.DeviceClass
	}{
		{
			features:    0,
			expectClass: network.DeviceClassEthernet,
		},
		{
			features:    fidlethernet.FeaturesWlanAp,
			expectClass: network.DeviceClassWlanAp,
		},
		{
			features:    fidlethernet.FeaturesSynthetic,
			expectClass: network.DeviceClassVirtual,
		},
		{
			features:    fidlethernet.FeaturesWlan,
			expectClass: network.DeviceClassWlan,
		},
		{
			features:    fidlethernet.FeaturesWlan | fidlethernet.FeaturesWlanAp,
			expectClass: network.DeviceClassWlanAp,
		},
	}
	for _, test := range tests {
		c := eth.Client{
			Info: fidlethernet.Info{
				Features: test.features,
			},
		}
		if got := c.DeviceClass(); got != test.expectClass {
			t.Errorf("got c.DeviceClass() = %s, want = %s", got, test.expectClass)
		}
	}
}
