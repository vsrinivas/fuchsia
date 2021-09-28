// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain
// +build !build_with_native_toolchain

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
	"sync"
	"syscall/zx"
	"testing"
	"time"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/testutil"
	"go.fuchsia.dev/fuchsia/src/lib/component"
	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	"fidl/fuchsia/hardware/network"
	"fidl/fuchsia/net"
	"fidl/fuchsia/net/tun"

	"github.com/google/go-cmp/cmp"
	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/buffer"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/link/ethernet"
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

func createTunWithOnline(t *testing.T, ctx context.Context, online bool) (*tun.DeviceWithCtxInterface, *tun.PortWithCtxInterface) {
	portConfig := defaultPortConfig()
	portConfig.SetOnline(online)

	tunDevice := createTunDeviceOnly(t, ctx)
	tunPort := addPortWithConfig(t, ctx, tunDevice, portConfig)
	return tunDevice, tunPort
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

func newClientAndPort(t *testing.T, ctx context.Context, netdev *network.DeviceWithCtxInterface) (*Client, *Port) {
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

	port, err := client.NewPort(ctx, TunPortId)
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
	tundev, tunport := createTunWithOnline(t, ctx, online)
	netdev := connectDevice(t, ctx, tundev)
	client, port := newClientAndPort(t, ctx, netdev)
	return tundev, tunport, client, port
}

func runClient(t *testing.T, client *Client) {
	ctx, cancel := context.WithCancel(context.Background())
	var wg sync.WaitGroup
	wg.Add(1)
	go func() {
		client.Run(ctx)
		wg.Done()
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

func TestClient_WritePacket(t *testing.T) {
	ctx := context.Background()

	tunDev, _, client, port := createTunClientPairWithOnline(t, ctx, false)
	runClient(t, client)
	defer func() {
		if err := tunDev.Close(); err != nil {
			t.Fatalf("tunDev.Close() failed: %s", err)
		}
		port.Wait()
	}()

	linkEndpoint := ethernet.New(port)

	port.SetOnLinkClosed(func() {})
	port.SetOnLinkOnlineChanged(func(bool) {})

	dispatcher := make(dispatcherChan)
	close(dispatcher)
	linkEndpoint.Attach(&dispatcher)

	if err := port.Up(); err != nil {
		t.Fatalf("port.Up() = %s", err)
	}

	if err := linkEndpoint.WritePacket(stack.RouteInfo{}, header.IPv4ProtocolNumber, stack.NewPacketBuffer(stack.PacketBufferOptions{
		ReserveHeaderBytes: int(linkEndpoint.MaxHeaderLength()),
	})); err != nil {
		t.Fatalf("WritePacket failed: %s", err)
	}

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

func TestWritePacket(t *testing.T) {
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
			tunDev, _ := createTunWithOnline(t, ctx, true)
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

			port, err := client.NewPort(ctx, TunPortId)
			if err != nil {
				t.Fatalf("client.NewPort(_, %d) failed: %s", TunPortId, err)
			}
			t.Cleanup(func() {
				if err := port.Close(); err != nil {
					t.Errorf("port close failed: %s", err)
				}
			})
			port.SetOnLinkClosed(func() {})
			port.SetOnLinkOnlineChanged(func(bool) {})

			linkEndpoint := ethernet.New(port)

			dispatcher := make(dispatcherChan)
			linkEndpoint.Attach(&dispatcher)

			if err := port.Up(); err != nil {
				t.Fatalf("port.Up() = %s", err)
			}
			tunMac := getTunMac()
			otherMac := getOtherMac()
			const protocol = tcpip.NetworkProtocolNumber(45)
			const pktBody = "bar"
			var r stack.RouteInfo
			r.LocalLinkAddress = tcpip.LinkAddress(tunMac.Octets[:])
			r.RemoteLinkAddress = tcpip.LinkAddress(otherMac.Octets[:])
			if err := linkEndpoint.WritePacket(
				r,
				protocol,
				stack.NewPacketBuffer(stack.PacketBufferOptions{
					ReserveHeaderBytes: int(linkEndpoint.MaxHeaderLength()),
					Data:               buffer.View(pktBody).ToVectorisedView(),
				}),
			); err != nil {
				t.Fatalf("WritePacket failed: %s", err)
			}
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
				ethType := [2]byte{0, 0}
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

func TestReceivePacket(t *testing.T) {
	ctx := context.Background()

	tunDev, _, client, port := createTunClientPair(t, ctx)
	runClient(t, client)

	port.SetOnLinkClosed(func() {})
	port.SetOnLinkOnlineChanged(func(bool) {})

	linkEndpoint := ethernet.New(port)

	if err := port.Up(); err != nil {
		t.Fatalf("port.Up() = %s", err)
	}

	dispatcher := make(dispatcherChan, 1)
	linkEndpoint.Attach(&dispatcher)

	tunMac := getTunMac()
	otherMac := getOtherMac()
	const protocol = tcpip.NetworkProtocolNumber(45)
	const pktPayload = "foobarbazfoobar"
	referenceFrame := func() []uint8 {
		b := tunMac.Octets[:]
		b = append(b, otherMac.Octets[:]...)
		ethType := [2]byte{0, 0}
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
		t.Fatalf("unexpected packet received: %#v", args)
	}

	ethFields := header.EthernetFields{
		SrcAddr: tcpip.LinkAddress(otherMac.Octets[:]),
		DstAddr: tcpip.LinkAddress(tunMac.Octets[:]),
		Type:    protocol,
	}
	wantLinkHdr := make(buffer.View, header.EthernetMinimumSize)
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
		args := <-dispatcher
		if diff := cmp.Diff(args, DeliverNetworkPacketArgs{
			SrcLinkAddr: tcpip.LinkAddress(otherMac.Octets[:]),
			DstLinkAddr: tcpip.LinkAddress(tunMac.Octets[:]),
			Protocol:    protocol,
			Pkt: func() *stack.PacketBuffer {
				vv := wantLinkHdr.ToVectorisedView()
				vv.AppendView(buffer.View(pktPayload[:extra]))
				pkt := stack.NewPacketBuffer(stack.PacketBufferOptions{
					Data: vv,
				})
				_, ok := pkt.LinkHeader().Consume(header.EthernetMinimumSize)
				if !ok {
					t.Fatalf("failed to consume %d bytes for link header", header.EthernetMinimumSize)
				}
				return pkt
			}(),
		}, testutil.PacketBufferCmpTransformer); diff != "" {
			t.Fatalf("delivered network packet mismatch (-want +got):\n%s", diff)
		}
	}

	if err := tunDev.Close(); err != nil {
		t.Fatalf("tunDev.Close() failed: %s", err)
	}
	linkEndpoint.Wait()
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
				client.Run(ctx)
				close(clientClosed)
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
			portId := PortId(index)
			portConfig := defaultPortConfig()
			portConfig.Base.SetRxTypes(testCase.frameTypes)
			portConfig.Base.SetId(portId)

			_ = addPortWithConfig(t, ctx, tunDev, portConfig)

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
				var got *invalidPortOperatingModeError
				if !errors.As(err, &got) {
					t.Fatalf("client.NewPort(_, %d) = %s, expected %T", portId, err, got)
				}
				if diff := cmp.Diff(got,
					&invalidPortOperatingModeError{
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

	lClient, lPort := newClientAndPort(t, ctx, left)
	rClient, rPort := newClientAndPort(t, ctx, right)
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
	rDispatcher := make(dispatcherChan, 1)
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

	makeTestPacket := func(prefix byte, index uint16) *stack.PacketBuffer {
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
		return stack.NewPacketBuffer(stack.PacketBufferOptions{
			Data: buffer.View(view).ToVectorisedView(),
		})
	}

	send := func(endpoint stack.LinkEndpoint, prefix byte, errs chan error) {
		for i := uint16(0); i < packetCount; i++ {
			if err := endpoint.WritePacket(stack.RouteInfo{}, header.IPv4ProtocolNumber, makeTestPacket(prefix, i)); err != nil {
				errs <- fmt.Errorf("WritePacket error: %s", err)
				return
			}
		}
		errs <- nil
	}

	validate := func(pkt DeliverNetworkPacketArgs, prefix uint8, index uint16) {
		if diff := cmp.Diff(pkt, DeliverNetworkPacketArgs{
			Protocol: header.IPv4ProtocolNumber,
			Pkt:      makeTestPacket(prefix, index),
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
			validate(pkt, rPrefix, lReceived)
			lReceived++
		case pkt := <-rDispatcher:
			validate(pkt, lPrefix, rReceived)
			rReceived++
		}

	}

	if err := pair.Close(); err != nil {
		t.Fatalf("tun pair close failed: %s", err)
	}
	lPort.Wait()
	rPort.Wait()
}
