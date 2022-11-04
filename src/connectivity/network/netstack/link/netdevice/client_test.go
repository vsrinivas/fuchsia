// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package netdevice

import (
	"bytes"
	"context"
	"encoding/binary"
	"errors"
	"fmt"
	"math/rand"
	"os"
	"runtime"
	"syscall/zx"
	"syscall/zx/zxwait"
	"testing"
	"time"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link/fifo"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/sync"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/testutil"
	"go.fuchsia.dev/fuchsia/src/lib/component"
	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	"fidl/fuchsia/hardware/network"
	"fidl/fuchsia/net"
	"fidl/fuchsia/net/tun"

	"github.com/google/go-cmp/cmp"
	"gvisor.dev/gvisor/pkg/bufferv2"
	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/link/ethernet"
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

var _ SessionConfigFactory = (*MockSessionConfigFactory)(nil)

type MockSessionConfigFactory struct {
	factory        SimpleSessionConfigFactory
	txHeaderLength uint16
	txTailLength   uint16
}

func (c *MockSessionConfigFactory) MakeSessionConfig(deviceInfo network.DeviceInfo) (SessionConfig, error) {
	config, err := c.factory.MakeSessionConfig(deviceInfo)
	if err == nil {
		config.TxHeaderLength = c.txHeaderLength
		config.TxTailLength = c.txTailLength
		config.BufferLength += uint32(c.txHeaderLength + c.txTailLength)
		config.BufferStride = config.BufferLength
	}

	return config, err
}

const TunMtu uint32 = 2048
const TunMinTxLength int = 60

// Use a nonzero port id to expose zero value bugs.
const TunPortId uint8 = 13

func getTunMac() net.MacAddress {
	return net.MacAddress{Octets: [6]uint8{0x02, 0x03, 0x04, 0x05, 0x06, 0x07}}
}
func getOtherMac() net.MacAddress {
	return net.MacAddress{Octets: [6]uint8{0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE}}
}

var lazyComponentContext struct {
	once sync.Once
	ctx  *component.Context
}

func componentCtx() *component.Context {
	lazyComponentContext.once.Do(func() { lazyComponentContext.ctx = component.NewContextFromStartupInfo() })
	return lazyComponentContext.ctx
}

func tunCtl(t *testing.T) *tun.ControlWithCtxInterface {
	t.Helper()
	req, tunCtl, err := tun.NewControlWithCtxInterfaceRequest()
	if err != nil {
		t.Fatalf("failed to create tunctl request: %s", err)
	}
	componentCtx().ConnectToEnvService(req)
	t.Cleanup(func() {
		if err := tunCtl.Close(); err != nil {
			t.Errorf("failed to close tunctl: %s", err)
		}
	})
	return tunCtl
}

func newTunDeviceRequest(t *testing.T) (tun.DeviceWithCtxInterfaceRequest, *tun.DeviceWithCtxInterface) {
	t.Helper()
	deviceRequest, device, err := tun.NewDeviceWithCtxInterfaceRequest()
	if err != nil {
		t.Fatalf("failed to create tun device request: %s", err)
	}
	t.Cleanup(func() {
		if err := device.Close(); err != nil {
			t.Errorf("tun device close failed: %s", err)
		}
	})
	return deviceRequest, device
}

func newTunPortRequest(t *testing.T) (tun.PortWithCtxInterfaceRequest, *tun.PortWithCtxInterface) {
	t.Helper()
	portRequest, port, err := tun.NewPortWithCtxInterfaceRequest()
	if err != nil {
		t.Fatalf("failed to create tun port request: %s", err)
	}
	t.Cleanup(func() {
		if err := port.Close(); err != nil {
			t.Errorf("tun port close failed: %s", err)
		}
	})
	return portRequest, port
}

func newNetworkDeviceRequest(t *testing.T) (network.DeviceWithCtxInterfaceRequest, *network.DeviceWithCtxInterface) {
	t.Helper()
	deviceRequest, device, err := network.NewDeviceWithCtxInterfaceRequest()
	if err != nil {
		t.Fatalf("failed to create network device request: %s", err)
	}
	t.Cleanup(func() {
		if err := device.Close(); err != nil {
			t.Errorf("network device close failed: %s", err)
		}
	})
	return deviceRequest, device
}

func newNetworkPortRequest(t *testing.T) (network.PortWithCtxInterfaceRequest, *network.PortWithCtxInterface) {
	t.Helper()
	portRequest, port, err := network.NewPortWithCtxInterfaceRequest()
	if err != nil {
		t.Fatalf("failed to create network port request: %s", err)
	}
	t.Cleanup(func() {
		if err := port.Close(); err != nil {
			t.Errorf("network port close failed: %s", err)
		}
	})
	return portRequest, port
}

func newTunDevicePairRequest(t *testing.T) (tun.DevicePairWithCtxInterfaceRequest, *tun.DevicePairWithCtxInterface) {
	t.Helper()
	deviceRequest, device, err := tun.NewDevicePairWithCtxInterfaceRequest()
	if err != nil {
		t.Fatalf("failed to create tun device pair request: %s", err)
	}
	t.Cleanup(func() {
		if err := device.Close(); err != nil {
			t.Errorf("tun device pair close failed: %s", err)
		}
	})
	return deviceRequest, device
}

func createTunWithConfig(t *testing.T, ctx context.Context, config tun.DeviceConfig) *tun.DeviceWithCtxInterface {
	t.Helper()
	deviceRequest, device := newTunDeviceRequest(t)
	if err := tunCtl(t).CreateDevice(ctx, config, deviceRequest); err != nil {
		t.Fatalf("tunCtl.CreateDevice failed: %s", err)
	}
	return device
}

func addPortWithConfig(t *testing.T, ctx context.Context, tunDev *tun.DeviceWithCtxInterface, config tun.DevicePortConfig) *tun.PortWithCtxInterface {
	t.Helper()
	portRequest, port := newTunPortRequest(t)
	if err := tunDev.AddPort(ctx, config, portRequest); err != nil {
		t.Fatalf("tundev.AddPort(_, _, _): %s", err)
	}
	return port
}

func defaultPortConfig() tun.DevicePortConfig {
	var portConfig tun.DevicePortConfig
	portConfig.SetMac(getTunMac())

	var basePortConfig tun.BasePortConfig
	basePortConfig.SetId(TunPortId)
	basePortConfig.SetMtu(TunMtu)
	basePortConfig.SetRxTypes([]network.FrameType{network.FrameTypeEthernet})
	basePortConfig.SetTxTypes([]network.FrameTypeSupport{{
		Type:           network.FrameTypeEthernet,
		Features:       network.FrameFeaturesRaw,
		SupportedFlags: 0,
	}})
	portConfig.SetBase(basePortConfig)
	return portConfig
}

func createTunDeviceOnly(t *testing.T, ctx context.Context) *tun.DeviceWithCtxInterface {
	t.Helper()

	var baseDeviceConfig tun.BaseDeviceConfig
	baseDeviceConfig.SetMinTxBufferLength(uint32(TunMinTxLength))

	var deviceConfig tun.DeviceConfig
	deviceConfig.SetBlocking(true)
	deviceConfig.SetBase(baseDeviceConfig)

	return createTunWithConfig(t, ctx, deviceConfig)
}

func getPortId(t *testing.T, ctx context.Context, getPort func(request network.PortWithCtxInterfaceRequest) error) network.PortId {
	t.Helper()
	portReq, port := newNetworkPortRequest(t)
	if err := getPort(portReq); err != nil {
		t.Fatalf("failed to send port request: %s", err)
	}
	info, err := port.GetInfo(ctx)
	if err != nil {
		t.Fatalf("port.GetInfo(_) = %s", err)
	}
	if !info.HasId() {
		t.Fatalf("missing id in port info: %+v", info)
	}
	return info.GetId()
}

func getTunPortId(t *testing.T, ctx context.Context, tunPort *tun.PortWithCtxInterface) network.PortId {
	t.Helper()
	return getPortId(t, ctx, func(req network.PortWithCtxInterfaceRequest) error {
		return tunPort.GetPort(ctx, req)
	})
}

func createTunWithOnline(t *testing.T, ctx context.Context, online bool) (*tun.DeviceWithCtxInterface, *tun.PortWithCtxInterface, network.PortId) {
	t.Helper()
	portConfig := defaultPortConfig()
	portConfig.SetOnline(online)

	tunDevice := createTunDeviceOnly(t, ctx)
	tunPort := addPortWithConfig(t, ctx, tunDevice, portConfig)
	portId := getTunPortId(t, ctx, tunPort)
	return tunDevice, tunPort, portId
}

func createTunPair(t *testing.T, ctx context.Context, frameTypes []network.FrameType) *tun.DevicePairWithCtxInterface {
	t.Helper()
	txTypes := make([]network.FrameTypeSupport, 0, len(frameTypes))
	for _, t := range frameTypes {
		txTypes = append(txTypes, network.FrameTypeSupport{
			Type:           t,
			Features:       network.FrameFeaturesRaw,
			SupportedFlags: 0,
		})
	}

	deviceRequest, device := newTunDevicePairRequest(t)
	if err := tunCtl(t).CreatePair(ctx, tun.DevicePairConfig{}, deviceRequest); err != nil {
		t.Fatalf("tunCtl.CreatePair failed: %s", err)
	}

	var basePortConfig tun.BasePortConfig
	basePortConfig.SetId(TunPortId)
	basePortConfig.SetMtu(TunMtu)
	basePortConfig.SetRxTypes(frameTypes)
	basePortConfig.SetTxTypes(txTypes)
	var portConfig tun.DevicePairPortConfig
	portConfig.SetBase(basePortConfig)
	portConfig.SetMacLeft(getTunMac())
	portConfig.SetMacRight(getOtherMac())

	result, err := device.AddPort(ctx, portConfig)
	if err != nil {
		t.Fatalf("device.AddPort(_, _): _, %s", err)
	}
	switch w := result.Which(); w {
	case tun.DevicePairAddPortResultResponse:
	case tun.DevicePairAddPortResultErr:
		t.Fatalf("device.AddPort(_, _): %s, nil", zx.Status(result.Err))
	default:
		t.Fatalf("device.AddPort(_, _) unrecognized result variant: %d", w)
	}
	return device
}

func createTunClientPair(t *testing.T, ctx context.Context) (*tun.DeviceWithCtxInterface, *tun.PortWithCtxInterface, *Client, *Port) {
	return createTunClientPairWithOnline(t, ctx, true)
}

func newClientAndPort(t *testing.T, ctx context.Context, netdev *network.DeviceWithCtxInterface, portId network.PortId) (*Client, *Port) {
	t.Helper()

	client, err := NewClient(ctx, netdev, &SimpleSessionConfigFactory{})
	if err != nil {
		t.Fatalf("NewClient failed: %s", err)
	}
	t.Cleanup(func() {
		if err := client.Close(); err != nil {
			t.Errorf("client close failed: %s", err)
		}
	})

	port, err := client.NewPort(ctx, portId)
	if err != nil {
		t.Fatalf("client.NewPort(_, %d) failed: %s", TunPortId, err)
	}
	t.Cleanup(func() {
		if err := port.Close(); err != nil {
			t.Errorf("port close failed: %s", err)
		}
	})

	return client, port
}

func createTunClientPairWithOnline(t *testing.T, ctx context.Context, online bool) (*tun.DeviceWithCtxInterface, *tun.PortWithCtxInterface, *Client, *Port) {
	t.Helper()
	tundev, tunport, portId := createTunWithOnline(t, ctx, online)
	netdev := connectDevice(t, ctx, tundev)
	client, port := newClientAndPort(t, ctx, netdev, portId)
	return tundev, tunport, client, port
}

func runClient(t *testing.T, client *Client) {
	ctx, cancel := context.WithCancel(context.Background())
	var wg sync.WaitGroup
	wg.Add(1)
	go func() {
		defer wg.Done()
		client.Run(ctx)
	}()
	t.Cleanup(func() {
		cancel()
		wg.Wait()
	})
}

func connectDevice(t *testing.T, ctx context.Context, tunDevice *tun.DeviceWithCtxInterface) *network.DeviceWithCtxInterface {
	t.Helper()
	devReq, dev := newNetworkDeviceRequest(t)
	if err := tunDevice.GetDevice(ctx, devReq); err != nil {
		t.Fatalf("tunDevice.GetDevice(_, _): %s", err)
	}
	return dev
}

func TestMain(m *testing.M) {
	syslog.SetVerbosity(syslog.DebugVerbosity)
	os.Exit(m.Run())
}

func TestClient_WritePackets(t *testing.T) {
	ctx := context.Background()

	tunDev, _, client, port := createTunClientPairWithOnline(t, ctx, false)
	runClient(t, client)
	defer func() {
		if err := tunDev.Close(); err != nil {
			t.Fatalf("tunDev.Close() failed: %s", err)
		}
		port.Wait()
	}()

	dispatcher := make(dispatcherChan)
	dispatcher.release()
	linkEndpoint := setupPortAndCreateEndpoint(t, port, &dispatcher)

	func() {
		var pkts stack.PacketBufferList
		defer pkts.DecRef()
		pkt := stack.NewPacketBuffer(stack.PacketBufferOptions{
			ReserveHeaderBytes: int(linkEndpoint.MaxHeaderLength()),
		})
		pkt.NetworkProtocolNumber = header.IPv4ProtocolNumber
		pkts.PushBack(pkt)

		linkEndpoint.AddHeader(pkt)
		if n, err := linkEndpoint.WritePackets(pkts); err != nil {
			t.Fatalf("WritePackets(_): %s", err)
		} else if n != 1 {
			t.Fatalf("got WritePackets(_) = %d, want = 1", n)
		}
	}()

	// This is unfortunate, but we don't have a way of being notified.
	now := time.Now()
	for {
		if client.handler.Stats.Tx.Drops.Value() == 0 {
			if time.Since(now) < 10*time.Second {
				runtime.Gosched()
				continue
			}
			t.Error("drop not incremented")
		}
		break
	}
}

func TestWritePackets(t *testing.T) {
	ctx := context.Background()

	tests := []struct {
		name           string
		txHeaderLength uint16
		txTailLength   uint16
	}{
		{
			name: "default",
		},
		{
			name:           "nonzero head + tail lengths",
			txHeaderLength: 2,
			txTailLength:   3,
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			tunDev, _, portId := createTunWithOnline(t, ctx, true)
			netdev := connectDevice(t, ctx, tunDev)
			client, err := NewClient(
				ctx,
				netdev,
				&MockSessionConfigFactory{
					factory:        SimpleSessionConfigFactory{},
					txHeaderLength: test.txHeaderLength,
					txTailLength:   test.txTailLength,
				},
			)
			if err != nil {
				t.Fatalf("NewClient failed: %s", err)
			}
			t.Cleanup(func() {
				if err := client.Close(); err != nil {
					t.Errorf("client close failed: %s", err)
				}
			})
			runClient(t, client)

			port, err := client.NewPort(ctx, portId)
			if err != nil {
				t.Fatalf("client.NewPort(_, %d) failed: %s", TunPortId, err)
			}
			t.Cleanup(func() {
				if err := port.Close(); err != nil {
					t.Errorf("port close failed: %s", err)
				}
			})

			dispatcher := make(dispatcherChan)
			defer dispatcher.release()
			linkEndpoint := setupPortAndCreateEndpoint(t, port, &dispatcher)

			tunMac := getTunMac()
			otherMac := getOtherMac()
			const protocol = tcpip.NetworkProtocolNumber(45)
			const pktBody = "bar"
			func() {
				var pkts stack.PacketBufferList
				defer pkts.DecRef()

				pkt := stack.NewPacketBuffer(stack.PacketBufferOptions{
					ReserveHeaderBytes: int(linkEndpoint.MaxHeaderLength()),
					Payload:            bufferv2.MakeWithData([]byte(pktBody)),
				})
				pkt.EgressRoute.LocalLinkAddress = tcpip.LinkAddress(tunMac.Octets[:])
				pkt.EgressRoute.RemoteLinkAddress = tcpip.LinkAddress(otherMac.Octets[:])
				pkt.NetworkProtocolNumber = protocol
				pkts.PushBack(pkt)
				linkEndpoint.AddHeader(pkt)

				if n, err := linkEndpoint.WritePackets(pkts); err != nil {
					t.Fatalf("WritePackets(_): %s", err)
				} else if n != 1 {
					t.Fatalf("got WritePackets(_)) = %d, want = 1", n)
				}
			}()
			readFrameResult, err := tunDev.ReadFrame(ctx)
			if err != nil {
				t.Fatalf("failed to read frame from tun device: %s", err)
			}
			if readFrameResult.Which() == tun.DeviceReadFrameResultErr {
				t.Fatalf("failed to read frame from tun: %s", zx.Status(readFrameResult.Err))
			}
			if got, want := readFrameResult.Response.Frame.FrameType, network.FrameTypeEthernet; got != want {
				t.Errorf("got Frame.FrameType = %d, want: %d", got, want)
			}
			if got, want := readFrameResult.Response.Frame.Port, TunPortId; got != want {
				t.Errorf("got Frame.Port = %d, want: %d", got, want)
			}
			data := readFrameResult.Response.Frame.Data

			expect := func() []byte {
				b := make([]byte, 0, TunMinTxLength)
				b = append(b, otherMac.Octets[:]...)
				b = append(b, tunMac.Octets[:]...)
				var ethType [2]byte
				binary.BigEndian.PutUint16(ethType[:], uint16(protocol))
				b = append(b, ethType[:]...)
				b = append(b, []byte(pktBody)...)
				if len(b) < TunMinTxLength {
					b = b[:TunMinTxLength]
				}
				return b
			}()
			if !bytes.Equal(data, expect) {
				t.Fatalf("delivered packet mismatch. Wanted %x,  got: %x", expect, data)
			}

			// The Tx descriptors are allocated sequentially after the Rx descriptors.
			// Therefore, the first Tx descriptor has index == count(rx descriptors).
			for i := client.config.RxDescriptorCount; i < (client.config.RxDescriptorCount + client.config.TxDescriptorCount); i++ {
				descriptor := client.getDescriptor(i)
				if got, want := descriptor.head_length, test.txHeaderLength; uint16(got) != want {
					t.Errorf("got Tx head_length = %d, want = %d", got, want)
				}
				if got, want := descriptor.tail_length, test.txTailLength; uint16(got) != want {
					t.Errorf("got Tx tail_length = %d, want = %d", got, want)
				}
			}

			if err := tunDev.Close(); err != nil {
				t.Fatalf("tunDev.Close() failed: %s", err)
			}
			port.Wait()
		})
	}
}

func setupPortAndCreateEndpointWithOnLinkClose(t *testing.T, port *Port, dispatcher *dispatcherChan, onLinkClosed func()) *ethernet.Endpoint {
	t.Helper()

	port.SetOnLinkClosed(onLinkClosed)
	port.SetOnLinkOnlineChanged(func(bool) {})

	linkEndpoint := ethernet.New(port)

	if err := port.Up(); err != nil {
		t.Fatalf("port.Up() = %s", err)
	}

	linkEndpoint.Attach(dispatcher)

	return linkEndpoint
}

func setupPortAndCreateEndpoint(t *testing.T, port *Port, dispatcher *dispatcherChan) *ethernet.Endpoint {
	return setupPortAndCreateEndpointWithOnLinkClose(t, port, dispatcher, func() {})
}

func TestReceivePacket(t *testing.T) {
	ctx := context.Background()

	tunDev, _, client, port := createTunClientPair(t, ctx)
	defer func() {
		if err := tunDev.Close(); err != nil {
			t.Fatalf("tunDev.Close() failed: %s", err)
		}
		port.Wait()
	}()

	runClient(t, client)

	dispatcher := make(dispatcherChan, 1)
	defer dispatcher.release()
	_ = setupPortAndCreateEndpoint(t, port, &dispatcher)

	tunMac := getTunMac()
	otherMac := getOtherMac()
	const protocol = tcpip.NetworkProtocolNumber(45)
	const pktPayload = "foobarbazfoobar"
	referenceFrame := func() []uint8 {
		b := tunMac.Octets[:]
		b = append(b, otherMac.Octets[:]...)
		var ethType [2]byte
		binary.BigEndian.PutUint16(ethType[:], uint16(protocol))
		b = append(b, ethType[:]...)
		b = append(b, []byte(pktPayload)...)
		return b
	}()
	send := func(sendSize int) {
		var frame tun.Frame
		frame.SetFrameType(network.FrameTypeEthernet)
		frame.SetData(referenceFrame[:sendSize])
		frame.SetPort(TunPortId)
		status, err := tunDev.WriteFrame(ctx, frame)
		if err != nil {
			t.Fatalf("WriteFrame failed: %s", err)
		}
		if status.Which() == tun.DeviceWriteFrameResultErr {
			t.Fatalf("unexpected error on WriteFrame: %s", zx.Status(status.Err))
		}
	}
	// First test that if we send something smaller than the minimum Ethernet frame size will not get dispatched.
	send(header.EthernetMinimumSize - 1)
	select {
	case <-time.After(200 * time.Millisecond):
	case args := <-dispatcher:
		args.Pkt.DecRef()
		t.Fatalf("unexpected packet received: %#v", args)
	}

	ethFields := header.EthernetFields{
		SrcAddr: tcpip.LinkAddress(otherMac.Octets[:]),
		DstAddr: tcpip.LinkAddress(tunMac.Octets[:]),
		Type:    protocol,
	}
	wantLinkHdr := make([]byte, header.EthernetMinimumSize)
	header.Ethernet(wantLinkHdr).Encode(&ethFields)

	// Then test some valid configurations.
	for _, extra := range []int{
		// Test receiving a frame that is equal to the minimum frame size.
		0,
		// Test receiving a frame that is just greater than the minimum frame size.
		1,
		// Test receiving the full frame.
		len(pktPayload),
	} {
		send(header.EthernetMinimumSize + extra)
		func() {
			args, ok := <-dispatcher
			if !ok {
				t.Fatal("expected to receive packet from dispatcher")
			}
			defer args.Pkt.DecRef()

			vv := bufferv2.MakeWithData(wantLinkHdr)
			vv.Append(bufferv2.NewViewWithData([]byte(pktPayload[:extra])))
			pkt := stack.NewPacketBuffer(stack.PacketBufferOptions{
				Payload: vv,
			})
			defer pkt.DecRef()
			if _, ok := pkt.LinkHeader().Consume(header.EthernetMinimumSize); !ok {
				t.Fatalf("failed to consume %d bytes for link header", header.EthernetMinimumSize)
			}
			if diff := cmp.Diff(args, DeliverNetworkPacketArgs{
				Protocol: protocol,
				Pkt:      pkt,
			}, testutil.PacketBufferCmpTransformer); diff != "" {
				t.Fatalf("delivered network packet mismatch (-want +got):\n%s", diff)
			}
		}()
	}
}

func TestReceivePacketNoMemoryLeak(t *testing.T) {
	ctx := context.Background()

	tunDev, _, client, port := createTunClientPair(t, ctx)
	defer func() {
		if err := tunDev.Close(); err != nil {
			t.Fatalf("tunDev.Close() failed: %s", err)
		}
		port.Wait()
	}()

	runClient(t, client)

	dispatcher := make(dispatcherChan, 1)
	defer dispatcher.release()
	_ = setupPortAndCreateEndpoint(t, port, &dispatcher)

	const protocol = tcpip.NetworkProtocolNumber(45)
	const pktPayload = "foobarbazfoobar"
	tunMac := getTunMac()
	otherMac := getOtherMac()

	referenceFrame := tunMac.Octets[:]
	referenceFrame = append(referenceFrame, otherMac.Octets[:]...)
	var ethType [2]byte
	binary.BigEndian.PutUint16(ethType[:], uint16(protocol))
	referenceFrame = append(referenceFrame, ethType[:]...)
	referenceFrame = append(referenceFrame, []byte(pktPayload)...)

	var frame tun.Frame
	frame.SetFrameType(network.FrameTypeEthernet)
	frame.SetData(referenceFrame)
	frame.SetPort(TunPortId)

	// Measured in December 2021:
	// * performing 10k runs takes ~1.3s.
	// * heap object growth is ranging from negative 50 to 0.
	if err := testutil.CheckHeapObjectsGrowth(10000, 100, func() {
		status, err := tunDev.WriteFrame(ctx, frame)
		if err != nil {
			t.Fatalf("WriteFrame failed: %s", err)
		}
		if status.Which() == tun.DeviceWriteFrameResultErr {
			t.Fatalf("unexpected error on WriteFrame: %s", zx.Status(status.Err))
		}
		pkt := (<-dispatcher).Pkt
		(&pkt).DecRef()
	}); err != nil {
		t.Fatal(err)
	}
}

func TestUpDown(t *testing.T) {
	ctx := context.Background()

	_, tunPort, _, port := createTunClientPair(t, ctx)

	state, err := tunPort.WatchState(ctx)
	if err != nil {
		t.Fatalf("failed to get tun device state: %s", err)
	}

	// On start, HasSession should be false.
	if state.HasSession {
		t.Fatalf("unexpected initial state: got %t, want false", state.HasSession)
	}

	// Call Up and retrieve the updated state from TunDev, checking if it is
	// powered now.
	if err := port.Up(); err != nil {
		t.Fatalf("port.Up() = %s", err)
	}
	state, err = tunPort.WatchState(ctx)
	if err != nil {
		t.Fatalf("failed to get tun device state: %s", err)
	}
	if !state.HasSession {
		t.Fatalf("unexpected state after Up: got %t, want true", state.HasSession)
	}

	// Call Down and retrieve the updated state from TunDev, checking if it is
	// not powered again.
	if err := port.Down(); err != nil {
		t.Fatalf("port.Down() = %s", err)
	}
	state, err = tunPort.WatchState(ctx)
	if err != nil {
		t.Fatalf("tunPort.WatchState(_) = %s", err)
	}
	if got, want := state.HasSession, false; got != want {
		t.Fatalf("got state.HasSession = %t, want %t", got, want)
	}
}

func TestSetPromiscuousMode(t *testing.T) {
	ctx := context.Background()

	_, tunPort, _, port := createTunClientPair(t, ctx)

	state, err := tunPort.WatchState(ctx)
	if err != nil {
		t.Fatalf("tunPort.WatchState(_) = %s", err)
	}
	// We always set the interface to multicast promiscuous when we create the
	// device. That might not be true once we have fine grained multicast filter
	// control.
	if !state.MacPresent || state.Mac.Mode != network.MacFilterModeMulticastPromiscuous {
		t.Fatalf("unexpected initial state %+v, expected state.Mac.Mode=%s", state, state.Mac.Mode)
	}

	// Set promiscuous mode to true and check that the mode changed with
	// tunDevice.
	if err := port.SetPromiscuousMode(true); err != nil {
		t.Fatalf("port.SetPromiscuousMode(true) = %s", err)
	}
	state, err = tunPort.WatchState(ctx)
	if err != nil {
		t.Fatalf("tunPort.WatchState(_) = %s", err)
	}
	if !state.MacPresent || state.Mac.Mode != network.MacFilterModePromiscuous {
		t.Fatalf("unexpected state after setting promiscuous mode ON %+v, expected state.Mac.Mode=%s", state, network.MacFilterModePromiscuous)
	}

	// Set promiscuous mode to false and check that the mode changed with
	// tunDevice.
	if err := port.SetPromiscuousMode(false); err != nil {
		t.Fatalf("port.SetPromiscuousMode(false) = %s", err)
	}
	state, err = tunPort.WatchState(ctx)
	if err != nil {
		t.Fatalf("tunPort.WatchState(_) = %s", err)
	}
	if !state.MacPresent || state.Mac.Mode != network.MacFilterModeMulticastPromiscuous {
		t.Fatalf("unexpected state after setting promiscuous mode OFF %+v, expected state.Mac.Mode=%s", state, network.MacFilterModeMulticastPromiscuous)
	}
}

func TestStateChange(t *testing.T) {
	ctx := context.Background()

	_, tunPort, client, port := createTunClientPair(t, ctx)

	closed := make(chan struct{})
	port.SetOnLinkClosed(func() { close(closed) })

	defer func() {
		// Close client and expect callback to fire.
		if err := client.Close(); err != nil {
			t.Fatalf("failed to close client: %s", err)
		}
		<-closed
	}()

	ch := make(chan bool, 1)
	port.SetOnLinkOnlineChanged(func(linkOnline bool) {
		ch <- linkOnline
	})

	dispatcher := make(dispatcherChan)
	defer dispatcher.release()
	port.Attach(&dispatcher)

	// First link state should be Started, because  we set tunDev to online by
	// default.
	if !<-ch {
		t.Error("initial link state down, want up")
	}

	// Set offline and expect link state Down.
	if err := tunPort.SetOnline(ctx, false); err != nil {
		t.Fatalf("tunPort.SetOnline(_, false) = %s", err)
	}
	if <-ch {
		t.Error("post-down link state up, want down")
	}

	// Set online and expect link state Started again.
	if err := tunPort.SetOnline(ctx, true); err != nil {
		t.Fatalf("tunPort.SetOnline(_, true) = %s", err)
	}
	if !<-ch {
		t.Error("post-up link state down, want up")
	}
}

func TestShutdown(t *testing.T) {
	type testData struct {
		tunDev       *tun.DeviceWithCtxInterface
		tunPort      *tun.PortWithCtxInterface
		port         *Port
		clientCancel context.CancelFunc
	}
	tests := []struct {
		name              string
		expectDeviceClose bool
		shutdown          func(t *testing.T, data testData)
	}{
		{
			// Both client and port should close when the tun device is destroyed.
			name:              "destroy device",
			expectDeviceClose: true,
			shutdown: func(t *testing.T, data testData) {
				if err := data.tunDev.Close(); err != nil {
					t.Fatalf("data.tunDev.Close() = %s", err)
				}
			},
		},
		{
			// Port should close when tun port is destroyed.
			name: "destroy port",
			shutdown: func(t *testing.T, data testData) {
				if err := data.tunPort.Close(); err != nil {
					t.Fatalf("data.tunPort.Close() = %s", err)
				}
			},
		},
		{
			// Port should close on detach.
			name: "detach port",
			shutdown: func(t *testing.T, data testData) {
				data.port.Attach(nil)
			},
		},
		{
			// Both client and port should close on client cancel.
			name:              "client cancel",
			expectDeviceClose: true,
			shutdown: func(t *testing.T, data testData) {
				data.clientCancel()
			},
		},
	}

	for _, testCase := range tests {
		t.Run(testCase.name, func(t *testing.T) {
			ctx := context.Background()

			tunDev, tunPort, client, port := createTunClientPair(t, ctx)
			var wg sync.WaitGroup
			wg.Add(1)

			ctx, cancel := context.WithCancel(ctx)
			clientClosed := make(chan struct{})
			go func() {
				defer close(clientClosed)
				client.Run(ctx)
			}()
			t.Cleanup(func() {
				// Always cleanup goroutine in case of early exit.
				cancel()
				<-clientClosed
			})

			portClosed := make(chan struct{})
			port.SetOnLinkClosed(func() { close(portClosed) })
			port.SetOnLinkOnlineChanged(func(bool) {})

			dispatcher := make(dispatcherChan)
			defer dispatcher.release()
			port.Attach(&dispatcher)

			testCase.shutdown(t, testData{
				tunDev:       tunDev,
				tunPort:      tunPort,
				port:         port,
				clientCancel: cancel,
			})

			// All cases shutdown port somehow.
			<-portClosed
			port.Wait()

			// Check if device is closed.
			if testCase.expectDeviceClose {
				<-clientClosed
				return
			}

			select {
			case <-clientClosed:
				t.Errorf("expected device to still be running")
			case <-time.After(50 * time.Millisecond):
			}
		})
	}
}

func TestPortModeDetection(t *testing.T) {
	tests := []struct {
		name       string
		frameTypes []network.FrameType
		mode       PortMode
		fails      bool
	}{
		{
			name:       "ethernet",
			frameTypes: []network.FrameType{network.FrameTypeEthernet},
			mode:       PortModeEthernet,
		},
		{
			name:       "ip",
			frameTypes: []network.FrameType{network.FrameTypeIpv4, network.FrameTypeIpv6},
			mode:       PortModeIp,
		},
		{
			name:       "invalid ip",
			frameTypes: []network.FrameType{network.FrameTypeIpv4},
			fails:      true,
		},
	}

	ctx := context.Background()
	tunDev := createTunDeviceOnly(t, ctx)
	dev := connectDevice(t, ctx, tunDev)
	client, err := NewClient(ctx, dev, &SimpleSessionConfigFactory{})
	if err != nil {
		t.Fatalf("NewClient(_, _, _) = %s", err)
	}
	defer func() {
		if err := client.Close(); err != nil {
			t.Fatalf("client.Close() = %s", err)
		}
	}()

	for index, testCase := range tests {
		t.Run(testCase.name, func(t *testing.T) {
			baseId := basePortId(index)
			portConfig := defaultPortConfig()
			portConfig.Base.SetRxTypes(testCase.frameTypes)
			portConfig.Base.SetId(baseId)

			tunPort := addPortWithConfig(t, ctx, tunDev, portConfig)
			portId := getTunPortId(t, ctx, tunPort)

			port, err := client.NewPort(ctx, portId)
			if err == nil {
				defer func() {
					if err := port.Close(); err != nil {
						t.Fatalf("port.Close() = %s", err)
					}
				}()
			}

			if testCase.fails {
				// Expect error.
				var got *InvalidPortOperatingModeError
				if !errors.As(err, &got) {
					t.Fatalf("client.NewPort(_, %d) = %s, expected %T", portId, err, got)
				}
				if diff := cmp.Diff(got,
					&InvalidPortOperatingModeError{
						rxTypes: testCase.frameTypes,
					}, cmp.AllowUnexported(*got)); diff != "" {
					t.Fatalf("client.NewPort(_, %d) error diff (-want +got):\n%s", portId, diff)
				}
				return
			}

			if err != nil {
				t.Fatalf("client.NewPort(_, %d) = %s", portId, err)
			}

			if got, want := port.Mode(), testCase.mode; want != got {
				t.Fatalf("got port.Mode() = %d, want = %d", got, want)
			}
		})
	}

}

func TestPairExchangePackets(t *testing.T) {
	ctx := context.Background()
	pair := createTunPair(t, ctx, []network.FrameType{network.FrameTypeIpv4, network.FrameTypeIpv6})

	leftRequest, left := newNetworkDeviceRequest(t)
	rightRequest, right := newNetworkDeviceRequest(t)

	if err := pair.GetLeft(ctx, leftRequest); err != nil {
		t.Fatalf("pair.GetLeft(_, _): %s", err)
	}
	if err := pair.GetRight(ctx, rightRequest); err != nil {
		t.Fatalf("pair.GetRight(_, _): %s", err)
	}

	lPortId := getPortId(t, ctx, func(req network.PortWithCtxInterfaceRequest) error {
		return pair.GetLeftPort(ctx, TunPortId, req)
	})
	rPortId := getPortId(t, ctx, func(req network.PortWithCtxInterfaceRequest) error {
		return pair.GetRightPort(ctx, TunPortId, req)
	})

	lClient, lPort := newClientAndPort(t, ctx, left, lPortId)
	rClient, rPort := newClientAndPort(t, ctx, right, rPortId)
	runClient(t, lClient)
	runClient(t, rClient)

	portInfo := []*struct {
		port   *Port
		online chan struct{}
	}{
		{port: lPort}, {port: rPort},
	}

	for _, info := range portInfo {
		ch := make(chan struct{})
		info.online = ch
		info.port.SetOnLinkClosed(func() {})
		info.port.SetOnLinkOnlineChanged(func(online bool) {
			if online {
				close(ch)
			}
		})
	}

	lDispatcher := make(dispatcherChan, 1)
	defer lDispatcher.release()
	rDispatcher := make(dispatcherChan, 1)
	defer rDispatcher.release()
	lPort.Attach(&lDispatcher)
	rPort.Attach(&rDispatcher)
	packetCount := lClient.deviceInfo.RxDepth * 4

	if err := lPort.Up(); err != nil {
		t.Fatalf("lPort.Up() =  %s", err)
	}
	if err := rPort.Up(); err != nil {
		t.Fatalf("rPort.Up() =  %s", err)
	}

	// Wait for both ports to come online.
	for _, info := range portInfo {
		<-info.online
	}

	makeTestPacket := func(prefix byte, index uint16) stack.PacketBufferPtr {
		rng := rand.New(rand.NewSource(int64(index)))

		view := []byte{prefix}

		var indexBuffer [2]byte
		binary.LittleEndian.PutUint16(indexBuffer[:], 0)
		view = append(view, indexBuffer[:]...)

		// Use randomized payload lengths so resetting descriptors is exercised
		// and verified.
		payloadLength := rng.Uint32() % (DefaultBufferLength - uint32(len(view)))
		for i := uint32(0); i < payloadLength; i++ {
			view = append(view, byte(rng.Uint32()))
		}
		pkt := stack.NewPacketBuffer(stack.PacketBufferOptions{
			Payload: bufferv2.MakeWithData(view),
		})
		pkt.NetworkProtocolNumber = header.IPv4ProtocolNumber
		return pkt
	}

	send := func(endpoint stack.LinkEndpoint, prefix byte, errs chan error) {
		var pkts stack.PacketBufferList
		defer pkts.DecRef()
		for i := uint16(0); i < packetCount; i++ {
			pkts.PushBack(makeTestPacket(prefix, i))
		}
		errs <- func() error {
			n, err := endpoint.WritePackets(pkts)
			if err != nil {
				return fmt.Errorf("WritePackets(_): %s", err)
			}
			if n != int(packetCount) {
				return fmt.Errorf("got WritePackets(_) = %d, want %d", n, packetCount)
			}
			return nil
		}()
	}

	validate := func(pktArgs DeliverNetworkPacketArgs, prefix uint8, index uint16) {
		pkt := makeTestPacket(prefix, index)
		defer pkt.DecRef()
		if diff := cmp.Diff(pktArgs, DeliverNetworkPacketArgs{
			Protocol: header.IPv4ProtocolNumber,
			Pkt:      pkt,
		}, testutil.PacketBufferCmpTransformer); diff != "" {
			t.Fatalf("delivered network packet mismatch (prefix=%d, index=%d) (-want +got):\n%s", prefix, index, diff)
		}
	}

	lSendErrs := make(chan error, 1)
	rSendErrs := make(chan error, 1)

	const lPrefix = 1
	const rPrefix = 2

	go send(lPort, lPrefix, lSendErrs)
	go send(rPort, rPrefix, rSendErrs)

	var rReceived, lReceived uint16
	for lReceived < packetCount || rReceived < packetCount {
		select {
		case err := <-lSendErrs:
			if err != nil {
				t.Fatalf("left send failed: %s", err)
			}
		case err := <-rSendErrs:
			if err != nil {
				t.Fatalf("left send failed: %s", err)
			}
		case pkt := <-lDispatcher:
			func() {
				defer pkt.Pkt.DecRef()
				validate(pkt, rPrefix, lReceived)
			}()
			lReceived++
		case pkt := <-rDispatcher:
			func() {
				defer pkt.Pkt.DecRef()
				validate(pkt, lPrefix, rReceived)
			}()
			rReceived++
		}

	}

	if err := pair.Close(); err != nil {
		t.Fatalf("tun pair close failed: %s", err)
	}
	lPort.Wait()
	rPort.Wait()
}

func TestClosesVMOsIfDidntRun(t *testing.T) {
	ctx := context.Background()
	_, _, client, _ := createTunClientPair(t, ctx)

	vmos := []struct {
		name string
		vmo  fifo.MappedVMO
	}{
		{
			name: "data",
			vmo:  client.data,
		},
		{
			name: "descriptors",
			vmo:  client.descriptors,
		},
	}

	// Close the client and expect already closed errors on the VMOs.
	if err := client.Close(); err != nil {
		t.Fatalf("client.Close() = %s", err)
	}

	for _, vmo := range vmos {
		t.Run(vmo.name, func(t *testing.T) {
			data := vmo.vmo.GetData(0, 1)
			// Try to write into the unmapped region and expect a panic.
			defer func() {
				if r := recover(); r == nil {
					t.Errorf("expected to have observed a panic")
				}
			}()

			t.Errorf("data = %d, should've panicked", data[0])
		})
	}

}

func TestPortRemovalRespectsSalt(t *testing.T) {
	ctx := context.Background()
	tunDev, tunPort, client, portEP := createTunClientPairWithOnline(t, ctx, false)
	runClient(t, client)
	t.Cleanup(func() {
		if err := tunDev.Close(); err != nil {
			t.Fatalf("tunDev.Close() failed: %s", err)
		}
	})
	removeOld := make(chan struct{})
	oldRemoved := make(chan struct{})
	dispatcher := make(dispatcherChan)
	// Netstack will install a callback to close the endpoint when it
	// discovers the port is closed. In this test, we install a callback
	// under our control to determine when the old interface should be
	// removed.
	_ = setupPortAndCreateEndpointWithOnLinkClose(t, portEP, &dispatcher, func() {
		<-removeOld
		portEP.Attach(nil)
		oldRemoved <- struct{}{}
	})

	// Remove the port ...
	if err := tunPort.Remove(ctx); err != nil {
		t.Fatalf("tunPort.Remove(ctx) failed: %s", err)
	}
	if _, err := zxwait.WaitContext(ctx, *tunPort.Handle(), zx.SignalChannelPeerClosed); err != nil {
		t.Fatalf("failed to wait for the port to be removed from the tun device: %s", err)
	}
	portEP.Wait()
	dispatcher.release()

	// .. and add it back using the same ID.
	portConfig := defaultPortConfig()
	portConfig.SetOnline(true)
	tunPort = addPortWithConfig(t, ctx, tunDev, portConfig)
	portId := getTunPortId(t, ctx, tunPort)
	newPortEP, err := client.NewPort(ctx, portId)
	if err != nil {
		t.Fatalf("client.NewPort(_, %d) failed: %s", TunPortId, err)
	}
	t.Cleanup(func() {
		if err := newPortEP.Close(); err != nil {
			t.Errorf("port close failed: %s", err)
		}
	})
	newDispatcher := make(dispatcherChan, 1)
	t.Cleanup(func() {
		newDispatcher.release()
	})
	_ = setupPortAndCreateEndpoint(t, newPortEP, &newDispatcher)

	// Now remove the old one, we expect that our newly installed port will
	// not be removed even though it shares the same base port ID.
	removeOld <- struct{}{}
	<-oldRemoved
	client.mu.Lock()
	storedPortEP, present := client.mu.ports[TunPortId]
	client.mu.Unlock()
	if !present {
		t.Fatalf("got client.ports[%d] = (_, %t), want %t", TunPortId, present, true)
	}
	if got, want := storedPortEP.portInfo.GetId().Salt, newPortEP.portInfo.GetId().Salt; got != want {
		t.Fatalf("the stored port endpoint for %d is not the new one: got salt %d, want %d", TunPortId, got, want)
	}

	// And incoming packets can actually be received on the new port.
	const protocol = tcpip.NetworkProtocolNumber(42)
	tunMac := getTunMac()
	otherMac := getOtherMac()
	referenceFrame := tunMac.Octets[:]
	referenceFrame = append(referenceFrame, otherMac.Octets[:]...)
	var ethType [2]byte
	binary.BigEndian.PutUint16(ethType[:], uint16(protocol))
	referenceFrame = append(referenceFrame, ethType[:]...)

	var frame tun.Frame
	frame.SetFrameType(network.FrameTypeEthernet)
	frame.SetData(referenceFrame)
	frame.SetPort(TunPortId)
	status, err := tunDev.WriteFrame(ctx, frame)
	if err != nil {
		t.Fatalf("WriteFrame failed: %s", err)
	}
	if status.Which() == tun.DeviceWriteFrameResultErr {
		t.Fatalf("unexpected error on WriteFrame: %s", zx.Status(status.Err))
	}
	received := <-newDispatcher
	if got, want := received.Protocol, protocol; got != want {
		t.Fatalf("delivered network packet protocol mismatch, got %d, want %d", got, want)
	}
}
