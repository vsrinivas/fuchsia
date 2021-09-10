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

func (c *MockSessionConfigFactory) MakeSessionConfig(deviceInfo network.DeviceInfo, portStatus network.PortStatus) (SessionConfig, error) {
	config, err := c.factory.MakeSessionConfig(deviceInfo, portStatus)
	if err == nil {
		config.TxHeaderLength = c.txHeaderLength
		config.TxTailLength = c.txTailLength
	}

	return config, err
}

const TunMtu uint32 = 2048
const TunMinTxLength int = 60
const TunPortId uint8 = 0

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

func newMacAddressingRequest(t *testing.T) (network.MacAddressingWithCtxInterfaceRequest, *network.MacAddressingWithCtxInterface) {
	t.Helper()
	macRequest, mac, err := network.NewMacAddressingWithCtxInterfaceRequest()
	if err != nil {
		t.Fatalf("failed to create mac addressing request: %s", err)
	}
	t.Cleanup(func() {
		if err := mac.Close(); err != nil {
			t.Errorf("mac addressing close failed: %s", err)
		}
	})
	return macRequest, mac
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

func createTunWithOnline(t *testing.T, ctx context.Context, online bool) (*tun.DeviceWithCtxInterface, *tun.PortWithCtxInterface) {
	t.Helper()

	var baseDeviceConfig tun.BaseDeviceConfig
	baseDeviceConfig.SetMinTxBufferLength(uint32(TunMinTxLength))

	var deviceConfig tun.DeviceConfig
	deviceConfig.SetBlocking(true)
	deviceConfig.SetBase(baseDeviceConfig)

	var portConfig tun.DevicePortConfig
	portConfig.SetOnline(online)
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

	tunDevice := createTunWithConfig(t, ctx, deviceConfig)
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

func createTunClientPair(t *testing.T, ctx context.Context) (*tun.DeviceWithCtxInterface, *tun.PortWithCtxInterface, *MacAddressingClient) {
	return createTunClientPairWithOnline(t, ctx, true)
}

func createTunClientPairWithOnline(t *testing.T, ctx context.Context, online bool) (*tun.DeviceWithCtxInterface, *tun.PortWithCtxInterface, *MacAddressingClient) {
	t.Helper()
	tundev, tunport := createTunWithOnline(t, ctx, online)
	netdev, mac := connectProtos(t, ctx, tundev, TunPortId)

	client, err := NewMacAddressingClient(ctx, netdev, mac, &SimpleSessionConfigFactory{})
	if err != nil {
		t.Fatalf("NewMacAddressingClient failed: %s", err)
	}

	t.Cleanup(func() {
		if err := client.Close(); err != nil {
			t.Errorf("client close failed: %s", err)
		}
	})
	return tundev, tunport, client
}

func connectProtos(t *testing.T, ctx context.Context, tunDevice *tun.DeviceWithCtxInterface, portId uint8) (*network.DeviceWithCtxInterface, *network.MacAddressingWithCtxInterface) {
	t.Helper()
	devReq, dev := newNetworkDeviceRequest(t)
	macReq, mac := newMacAddressingRequest(t)
	portReq, port := newNetworkPortRequest(t)

	if err := tunDevice.GetDevice(ctx, devReq); err != nil {
		t.Fatalf("tunDevice.GetDevice(_, _): %s", err)
	}
	if err := dev.GetPort(ctx, portId, portReq); err != nil {
		t.Fatalf("dev.GetPort(_, %d, _): %s", portId, err)
	}
	if err := port.GetMac(ctx, macReq); err != nil {
		t.Fatalf("port.GetMac(_, _): %s", err)
	}
	return dev, mac
}

func TestMain(m *testing.M) {
	syslog.SetVerbosity(syslog.DebugVerbosity)
	os.Exit(m.Run())
}

func TestClient_WritePacket(t *testing.T) {
	ctx := context.Background()

	tunDev, _, client := createTunClientPairWithOnline(t, ctx, false)
	defer func() {
		if err := tunDev.Close(); err != nil {
			t.Fatalf("tunDev.Close() failed: %s", err)
		}
		client.Wait()
	}()

	linkEndpoint := ethernet.New(client)

	client.SetOnLinkClosed(func() {})
	client.SetOnLinkOnlineChanged(func(bool) {})

	dispatcher := make(dispatcherChan)
	close(dispatcher)
	linkEndpoint.Attach(&dispatcher)

	if err := client.Up(); err != nil {
		t.Fatalf("failed to start client %s", err)
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
			netdev, mac := connectProtos(t, ctx, tunDev, TunPortId)
			client, err := NewMacAddressingClient(
				ctx,
				netdev,
				mac,
				&MockSessionConfigFactory{
					factory:        SimpleSessionConfigFactory{},
					txHeaderLength: test.txHeaderLength,
					txTailLength:   test.txTailLength,
				},
			)
			if err != nil {
				t.Fatalf("NewMacAddressingClient failed: %s", err)
			}

			t.Cleanup(func() {
				if err := client.Close(); err != nil {
					t.Errorf("client close failed: %s", err)
				}
			})

			client.SetOnLinkClosed(func() {})
			client.SetOnLinkOnlineChanged(func(bool) {})

			linkEndpoint := ethernet.New(client)

			dispatcher := make(dispatcherChan)
			linkEndpoint.Attach(&dispatcher)

			if err := client.Up(); err != nil {
				t.Fatalf("failed to start client %s", err)
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
			if readFrameResult.Response.Frame.FrameType != network.FrameTypeEthernet {
				t.Errorf("unexpected response frame type: got %d, want: %d", readFrameResult.Response.Frame.FrameType, network.FrameTypeEthernet)
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

			if err := tunDev.Close(); err != nil {
				t.Fatalf("tunDev.Close() failed: %s", err)
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
			client.Wait()
		})
	}
}

func TestReceivePacket(t *testing.T) {
	ctx := context.Background()

	tunDev, _, client := createTunClientPair(t, ctx)

	client.SetOnLinkClosed(func() {})
	client.SetOnLinkOnlineChanged(func(bool) {})

	linkEndpoint := ethernet.New(client)

	if err := client.Up(); err != nil {
		t.Fatalf("failed to start client %s", err)
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

	_, tunPort, client := createTunClientPair(t, ctx)

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
	if err := client.Up(); err != nil {
		t.Fatalf("client.Up failed: %s", err)
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
	if err := client.Down(); err != nil {
		t.Fatalf("client.Down failed: %s", err)
	}
	state, err = tunPort.WatchState(ctx)
	if err != nil {
		t.Fatalf("failed to get tun device state: %s", err)
	}
	if state.HasSession {
		t.Fatalf("unexpected state after Down: got %t, want false", state.HasSession)
	}
}

func TestSetPromiscuousMode(t *testing.T) {
	ctx := context.Background()

	_, tunPort, client := createTunClientPair(t, ctx)

	state, err := tunPort.WatchState(ctx)
	if err != nil {
		t.Fatalf("failed to get tun device state: %s", err)
	}
	// We always set the interface to multicast promiscuous when we create the
	// device. That might not be true once we have fine grained multicast filter
	// control.
	if !state.MacPresent || state.Mac.Mode != network.MacFilterModeMulticastPromiscuous {
		t.Fatalf("unexpected initial state %+v, expected state.Mac.Mode=%s", state, state.Mac.Mode)
	}

	// Set promiscuous mode to true and check that the mode changed with
	// tunDevice.
	if err := client.SetPromiscuousMode(true); err != nil {
		t.Fatalf("failed to enable promiscuous mode: %s", err)
	}
	state, err = tunPort.WatchState(ctx)
	if err != nil {
		t.Fatalf("failed to get tun device state: %s", err)
	}
	if !state.MacPresent || state.Mac.Mode != network.MacFilterModePromiscuous {
		t.Fatalf("unexpected state after setting promiscuous mode ON %+v, expected state.Mac.Mode=%s", state, network.MacFilterModePromiscuous)
	}

	// Set promiscuous mode to false and check that the mode changed with
	// tunDevice.
	if err := client.SetPromiscuousMode(false); err != nil {
		t.Fatalf("failed to disable promiscuous mode: %s", err)
	}
	state, err = tunPort.WatchState(ctx)
	if err != nil {
		t.Fatalf("failed to get tun device state: %s", err)
	}
	if !state.MacPresent || state.Mac.Mode != network.MacFilterModeMulticastPromiscuous {
		t.Fatalf("unexpected state after setting promiscuous mode OFF %+v, expected state.Mac.Mode=%s", state, network.MacFilterModeMulticastPromiscuous)
	}
}

func TestStateChange(t *testing.T) {
	ctx := context.Background()

	_, tunPort, client := createTunClientPair(t, ctx)

	closed := make(chan struct{})
	client.SetOnLinkClosed(func() { close(closed) })

	defer func() {
		// Close and expect callback to fire.
		if err := client.Close(); err != nil {
			t.Fatalf("failed to close client: %s", err)
		}
		<-closed
	}()

	ch := make(chan bool, 1)
	client.SetOnLinkOnlineChanged(func(linkOnline bool) {
		ch <- linkOnline
	})

	dispatcher := make(dispatcherChan)
	client.Attach(&dispatcher)

	// First link state should be Started, because  we set tunDev to online by
	// default.
	if !<-ch {
		t.Error("initial link state down, want up")
	}

	// Set offline and expect link state Down.
	if err := tunPort.SetOnline(ctx, false); err != nil {
		t.Fatalf("failed to set device online: %s", err)
	}
	if <-ch {
		t.Error("post-down link state up, want down")
	}

	// Set online and expect link state Started again.
	if err := tunPort.SetOnline(ctx, true); err != nil {
		t.Fatalf("failed to set device offline: %s", err)
	}
	if !<-ch {
		t.Error("post-up link state down, want up")
	}
}

func TestDestroyDeviceCausesClose(t *testing.T) {
	ctx := context.Background()

	tunDev, _, client := createTunClientPair(t, ctx)

	closed := make(chan struct{})
	client.SetOnLinkClosed(func() { close(closed) })
	client.SetOnLinkOnlineChanged(func(bool) {})

	dispatcher := make(dispatcherChan)
	client.Attach(&dispatcher)

	// Close and expect callback to fire.
	if err := tunDev.Close(); err != nil {
		t.Fatalf("tunDev.Close() failed: %s", err)
	}
	<-closed
}

func TestCreationFailsIBadFrameType(t *testing.T) {
	ctx := context.Background()

	tunDev, _ := createTunWithOnline(t, ctx, true)

	dev, mac := connectProtos(t, ctx, tunDev, TunPortId)

	// Try to use IPv4 frames, but tun device is created only with Ethernet
	// frame support.
	if _, err := NewMacAddressingClient(ctx, dev, mac, &SimpleSessionConfigFactory{
		FrameTypes: []network.FrameType{network.FrameTypeIpv4},
	}); err == nil {
		t.Fatal("creating link endpoint with bad frame support should fail")
	}
}

func TestPairExchangePackets(t *testing.T) {
	ctx := context.Background()
	pair := createTunPair(t, ctx, []network.FrameType{network.FrameTypeIpv4})

	leftRequest, left := newNetworkDeviceRequest(t)
	rightRequest, right := newNetworkDeviceRequest(t)

	if err := pair.GetLeft(ctx, leftRequest); err != nil {
		t.Fatalf("pair.GetLeft(_, _): %s", err)
	}
	if err := pair.GetRight(ctx, rightRequest); err != nil {
		t.Fatalf("pair.GetRight(_, _): %s", err)
	}

	sessionConfig := SimpleSessionConfigFactory{FrameTypes: []network.FrameType{network.FrameTypeIpv4}}
	lClient, err := NewClient(ctx, left, &sessionConfig)
	if err != nil {
		t.Fatalf("failed to create left client: %s", err)
	}
	t.Cleanup(func() {
		if err := lClient.Close(); err != nil {
			t.Errorf("left client close failed: %s", err)
		}
	})

	rClient, err := NewClient(ctx, right, &sessionConfig)
	if err != nil {
		t.Fatalf("failed to create right client: %s", err)
	}
	t.Cleanup(func() {
		if err := rClient.Close(); err != nil {
			t.Errorf("right client close failed: %s", err)
		}
	})

	for _, client := range []*Client{lClient, rClient} {
		client.SetOnLinkClosed(func() {})
		client.SetOnLinkOnlineChanged(func(bool) {})
	}

	lDispatcher := make(dispatcherChan, 1)
	rDispatcher := make(dispatcherChan, 1)
	lClient.Attach(&lDispatcher)
	rClient.Attach(&rDispatcher)
	packetCount := lClient.deviceInfo.RxDepth * 4

	if err := lClient.Up(); err != nil {
		t.Fatalf("failed to start left client: %s", err)
	}
	if err := rClient.Up(); err != nil {
		t.Fatalf("failed to start right client: %s", err)
	}

	req, watcher, err := network.NewStatusWatcherWithCtxInterfaceRequest()
	if err != nil {
		t.Fatalf("failed to create status watcher request: %s", err)
	}
	t.Cleanup(func() {
		if err := watcher.Close(); err != nil {
			t.Errorf("watcher.Close() failed: %s", err)
		}
	})
	if err := lClient.port.GetStatusWatcher(ctx, req, network.MaxStatusBuffer); err != nil {
		t.Fatalf("failed to get status watcher: %s", err)
	}

	for {
		status, err := watcher.WatchStatus(context.Background())
		if err != nil {
			t.Fatalf("failed to get status: %s", err)
		}
		if status.GetFlags()&network.StatusFlagsOnline != 0 {
			break
		}
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

	go send(lClient, lPrefix, lSendErrs)
	go send(rClient, rPrefix, rSendErrs)

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
	lClient.Wait()
	rClient.Wait()
}
