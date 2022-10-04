// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"context"
	"errors"
	"fmt"
	"syscall/zx"
	"syscall/zx/fidl"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/fidlconv"
	"go.fuchsia.dev/fuchsia/src/lib/component"
	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	"fidl/fuchsia/net"
	"fidl/fuchsia/net/multicast/admin"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

func addMulticastIpv4RoutingTableControllerService(componentCtx *component.Context, stack *stack.Stack) {
	componentCtx.OutgoingService.AddService(
		admin.Ipv4RoutingTableControllerName,
		func(ctx context.Context, c zx.Channel) error {
			ctx, cancel := context.WithCancel(ctx)
			errorPathCleanup := cancel
			defer func() {
				if errorPathCleanup != nil {
					errorPathCleanup()
				}
			}()

			eventDispatcher := newMulticastEventDispatcher(cancel)
			alreadyEnabled, err := stack.EnableMulticastForwardingForProtocol(ipv4.ProtocolNumber, eventDispatcher)

			if err != nil {
				return WrapTcpIpError(err)
			}

			eventSender := admin.Ipv4RoutingTableControllerEventProxy(fidl.ChannelProxy{Channel: c})

			if alreadyEnabled {
				eventSender.OnClose(admin.TableControllerCloseReasonAlreadyInUse)
				return errors.New("Ipv4RoutingTableController already in use")
			}

			multicastIPv4Stub := admin.Ipv4RoutingTableControllerWithCtxStub{
				Impl: &multicastIpv4RoutingTableControllerImpl{
					stack:       stack,
					eventSender: eventSender,
					disp:        eventDispatcher,
				},
			}

			errorPathCleanup = nil
			go func() {
				defer cancel()
				component.Serve(ctx, &multicastIPv4Stub, c, component.ServeOptions{
					Concurrent: true,
					OnError: func(err error) {
						_ = syslog.WarnTf(admin.Ipv4RoutingTableControllerName, "%s", err)
					},
				})

				if err := stack.DisableMulticastForwardingForProtocol(ipv4.ProtocolNumber); err != nil {
					// Should never happen as this would imply that the IPv4
					// protocol implementation was removed.
					panic(fmt.Sprintf("stack.DisableMulticastForwardingForProtocol(ipv4.ProtocolNumber): %s", err))
				}
			}()

			return nil
		},
	)
}

var _ admin.Ipv4RoutingTableControllerWithCtx = (*multicastIpv4RoutingTableControllerImpl)(nil)

type multicastIpv4RoutingTableControllerImpl struct {
	stack       *stack.Stack
	eventSender admin.Ipv4RoutingTableControllerEventProxy
	disp        *multicastEventDispatcher
}

func toTCPIPAddressFromIPv4(addr net.Ipv4Address) tcpip.Address {
	return fidlconv.ToTCPIPAddress(net.IpAddressWithIpv4(addr))
}

func toStackUnicastSourceAndMulticastDestination(addresses admin.Ipv4UnicastSourceAndMulticastDestination) stack.UnicastSourceAndMulticastDestination {
	return stack.UnicastSourceAndMulticastDestination{
		Source:      toTCPIPAddressFromIPv4(addresses.UnicastSource),
		Destination: toTCPIPAddressFromIPv4(addresses.MulticastDestination),
	}
}

func (m *multicastIpv4RoutingTableControllerImpl) AddRoute(_ fidl.Context, addresses admin.Ipv4UnicastSourceAndMulticastDestination, route admin.Route) (admin.Ipv4RoutingTableControllerAddRouteResult, error) {
	multicastRoute, success := fidlconv.ToStackMulticastRoute(route)

	if !success {
		return admin.Ipv4RoutingTableControllerAddRouteResultWithErr(admin.Ipv4RoutingTableControllerAddRouteErrorRequiredRouteFieldsMissing), nil
	}

	stackAddresses := toStackUnicastSourceAndMulticastDestination(addresses)
	switch err := m.stack.AddMulticastRoute(ipv4.ProtocolNumber, stackAddresses, multicastRoute); err.(type) {
	case nil:
		return admin.Ipv4RoutingTableControllerAddRouteResultWithResponse(admin.Ipv4RoutingTableControllerAddRouteResponse{}), nil
	case *tcpip.ErrBadAddress:
		return admin.Ipv4RoutingTableControllerAddRouteResultWithErr(admin.Ipv4RoutingTableControllerAddRouteErrorInvalidAddress), nil
	case *tcpip.ErrMissingRequiredFields:
		return admin.Ipv4RoutingTableControllerAddRouteResultWithErr(admin.Ipv4RoutingTableControllerAddRouteErrorRequiredRouteFieldsMissing), nil
	case *tcpip.ErrMulticastInputCannotBeOutput:
		return admin.Ipv4RoutingTableControllerAddRouteResultWithErr(admin.Ipv4RoutingTableControllerAddRouteErrorInputCannotBeOutput), nil
	case *tcpip.ErrUnknownNICID:
		return admin.Ipv4RoutingTableControllerAddRouteResultWithErr(admin.Ipv4RoutingTableControllerAddRouteErrorInterfaceNotFound), nil
	default:
		panic(fmt.Sprintf("m.stack.AddRoute(ipv4.ProtocolNumber, %#v, %#v): %s", stackAddresses, multicastRoute, err))
	}
}

func (m *multicastIpv4RoutingTableControllerImpl) DelRoute(_ fidl.Context, addresses admin.Ipv4UnicastSourceAndMulticastDestination) (admin.Ipv4RoutingTableControllerDelRouteResult, error) {
	stackAddresses := toStackUnicastSourceAndMulticastDestination(addresses)
	switch err := m.stack.RemoveMulticastRoute(ipv4.ProtocolNumber, stackAddresses); err.(type) {
	case nil:
		return admin.Ipv4RoutingTableControllerDelRouteResultWithResponse(admin.Ipv4RoutingTableControllerDelRouteResponse{}), nil
	case *tcpip.ErrBadAddress:
		return admin.Ipv4RoutingTableControllerDelRouteResultWithErr(admin.Ipv4RoutingTableControllerDelRouteErrorInvalidAddress), nil
	case *tcpip.ErrNoRoute:
		return admin.Ipv4RoutingTableControllerDelRouteResultWithErr(admin.Ipv4RoutingTableControllerDelRouteErrorNotFound), nil
	default:
		panic(fmt.Sprintf("m.stack.RemoveMulticastRoute(ipv4.ProtocolNumber, %#v): %s", stackAddresses, err))
	}
}

func (m *multicastIpv4RoutingTableControllerImpl) GetRouteStats(_ fidl.Context, addresses admin.Ipv4UnicastSourceAndMulticastDestination) (admin.Ipv4RoutingTableControllerGetRouteStatsResult, error) {
	stackAddresses := toStackUnicastSourceAndMulticastDestination(addresses)
	switch timestamp, err := m.stack.MulticastRouteLastUsedTime(ipv4.ProtocolNumber, stackAddresses); err.(type) {
	case nil:
		var routeStats admin.RouteStats
		// The timestamp comes from the tcpip.Clock, which is backed by the
		// Fuchsia monotonic clock. As a result, the returned timestamp
		// corresponds to a value that all components can understand.
		routeStats.SetLastUsed(timestamp.Sub(tcpip.MonotonicTime{}).Nanoseconds())
		return admin.Ipv4RoutingTableControllerGetRouteStatsResultWithResponse(admin.Ipv4RoutingTableControllerGetRouteStatsResponse{Stats: routeStats}), nil
	case *tcpip.ErrBadAddress:
		return admin.Ipv4RoutingTableControllerGetRouteStatsResultWithErr(admin.Ipv4RoutingTableControllerGetRouteStatsErrorInvalidAddress), nil
	case *tcpip.ErrNoRoute:
		return admin.Ipv4RoutingTableControllerGetRouteStatsResultWithErr(admin.Ipv4RoutingTableControllerGetRouteStatsErrorNotFound), nil
	default:
		panic(fmt.Sprintf("m.stack.MulticastRouteLastUsedTime(ipv4.ProtocolNumber, %#v): %s", stackAddresses, err))
	}
}

func (m *multicastIpv4RoutingTableControllerImpl) WatchRoutingEvents(ctx fidl.Context) (uint64, admin.Ipv4UnicastSourceAndMulticastDestination, uint64, admin.RoutingEvent, error) {
	event, numDroppedEvents, err := m.disp.nextMulticastEvent(ctx, func() {
		m.eventSender.OnClose(admin.TableControllerCloseReasonHangingGetError)
	})

	if err != nil {
		return 0, admin.Ipv4UnicastSourceAndMulticastDestination{}, 0, admin.RoutingEvent{}, err
	}

	addresses := admin.Ipv4UnicastSourceAndMulticastDestination{
		UnicastSource:        fidlconv.ToNetIpAddress(event.context.SourceAndDestination.Source).Ipv4,
		MulticastDestination: fidlconv.ToNetIpAddress(event.context.SourceAndDestination.Destination).Ipv4,
	}

	return numDroppedEvents, addresses, uint64(event.context.InputInterface), event.event, nil
}
