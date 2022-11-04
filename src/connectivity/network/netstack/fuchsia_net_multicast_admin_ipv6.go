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
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv6"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

func addMulticastIpv6RoutingTableControllerService(componentCtx *component.Context, stack *stack.Stack) {
	componentCtx.OutgoingService.AddService(
		admin.Ipv6RoutingTableControllerName,
		func(ctx context.Context, c zx.Channel) error {
			ctx, cancel := context.WithCancel(ctx)
			errorPathCleanup := cancel
			defer func() {
				if errorPathCleanup != nil {
					errorPathCleanup()
				}
			}()

			eventDispatcher := newMulticastEventDispatcher(cancel)
			alreadyEnabled, err := stack.EnableMulticastForwardingForProtocol(ipv6.ProtocolNumber, eventDispatcher)

			if err != nil {
				return WrapTcpIpError(err)
			}

			eventSender := admin.Ipv6RoutingTableControllerEventProxy(fidl.ChannelProxy{Channel: c})

			if alreadyEnabled {
				eventSender.OnClose(admin.TableControllerCloseReasonAlreadyInUse)
				return errors.New("Ipv6RoutingTableController already in use")
			}

			multicastIPv6Stub := admin.Ipv6RoutingTableControllerWithCtxStub{
				Impl: &multicastIpv6RoutingTableControllerImpl{
					stack:       stack,
					eventSender: eventSender,
					disp:        eventDispatcher,
				},
			}

			errorPathCleanup = nil
			go func() {
				defer cancel()
				component.Serve(ctx, &multicastIPv6Stub, c, component.ServeOptions{
					Concurrent: true,
					OnError: func(err error) {
						_ = syslog.WarnTf(admin.Ipv6RoutingTableControllerName, "%s", err)
					},
				})

				if err := stack.DisableMulticastForwardingForProtocol(ipv6.ProtocolNumber); err != nil {
					// Should never happen as this would imply that the IPv6
					// protocol implementation was removed.
					panic(fmt.Sprintf("stack.DisableMulticastForwardingForProtocol(ipv6.ProtocolNumber): %s", err))
				}
			}()

			return nil
		},
	)
}

var _ admin.Ipv6RoutingTableControllerWithCtx = (*multicastIpv6RoutingTableControllerImpl)(nil)

type multicastIpv6RoutingTableControllerImpl struct {
	stack       *stack.Stack
	eventSender admin.Ipv6RoutingTableControllerEventProxy
	disp        *multicastEventDispatcher
}

func toTCPIPAddressFromIPv6(addr net.Ipv6Address) tcpip.Address {
	return fidlconv.ToTCPIPAddress(net.IpAddressWithIpv6(addr))
}

func toStackIPv6UnicastSourceAndMulticastDestination(addresses admin.Ipv6UnicastSourceAndMulticastDestination) stack.UnicastSourceAndMulticastDestination {
	return stack.UnicastSourceAndMulticastDestination{
		Source:      toTCPIPAddressFromIPv6(addresses.UnicastSource),
		Destination: toTCPIPAddressFromIPv6(addresses.MulticastDestination),
	}
}

func (m *multicastIpv6RoutingTableControllerImpl) AddRoute(_ fidl.Context, addresses admin.Ipv6UnicastSourceAndMulticastDestination, route admin.Route) (admin.Ipv6RoutingTableControllerAddRouteResult, error) {
	multicastRoute, success := fidlconv.ToStackMulticastRoute(route)

	if !success {
		return admin.Ipv6RoutingTableControllerAddRouteResultWithErr(admin.Ipv6RoutingTableControllerAddRouteErrorRequiredRouteFieldsMissing), nil
	}

	stackAddresses := toStackIPv6UnicastSourceAndMulticastDestination(addresses)
	switch err := m.stack.AddMulticastRoute(ipv6.ProtocolNumber, stackAddresses, multicastRoute); err.(type) {
	case nil:
		return admin.Ipv6RoutingTableControllerAddRouteResultWithResponse(admin.Ipv6RoutingTableControllerAddRouteResponse{}), nil
	case *tcpip.ErrBadAddress:
		return admin.Ipv6RoutingTableControllerAddRouteResultWithErr(admin.Ipv6RoutingTableControllerAddRouteErrorInvalidAddress), nil
	case *tcpip.ErrMissingRequiredFields:
		return admin.Ipv6RoutingTableControllerAddRouteResultWithErr(admin.Ipv6RoutingTableControllerAddRouteErrorRequiredRouteFieldsMissing), nil
	case *tcpip.ErrMulticastInputCannotBeOutput:
		return admin.Ipv6RoutingTableControllerAddRouteResultWithErr(admin.Ipv6RoutingTableControllerAddRouteErrorInputCannotBeOutput), nil
	case *tcpip.ErrUnknownNICID:
		return admin.Ipv6RoutingTableControllerAddRouteResultWithErr(admin.Ipv6RoutingTableControllerAddRouteErrorInterfaceNotFound), nil
	default:
		panic(fmt.Sprintf("m.stack.AddRoute(ipv6.ProtocolNumber, %#v, %#v): %s", stackAddresses, multicastRoute, err))
	}
}

func (m *multicastIpv6RoutingTableControllerImpl) DelRoute(_ fidl.Context, addresses admin.Ipv6UnicastSourceAndMulticastDestination) (admin.Ipv6RoutingTableControllerDelRouteResult, error) {
	stackAddresses := toStackIPv6UnicastSourceAndMulticastDestination(addresses)
	switch err := m.stack.RemoveMulticastRoute(ipv6.ProtocolNumber, stackAddresses); err.(type) {
	case nil:
		return admin.Ipv6RoutingTableControllerDelRouteResultWithResponse(admin.Ipv6RoutingTableControllerDelRouteResponse{}), nil
	case *tcpip.ErrBadAddress:
		return admin.Ipv6RoutingTableControllerDelRouteResultWithErr(admin.Ipv6RoutingTableControllerDelRouteErrorInvalidAddress), nil
	case *tcpip.ErrHostUnreachable:
		return admin.Ipv6RoutingTableControllerDelRouteResultWithErr(admin.Ipv6RoutingTableControllerDelRouteErrorNotFound), nil
	default:
		panic(fmt.Sprintf("m.stack.RemoveMulticastRoute(ipv6.ProtocolNumber, %#v): %s", stackAddresses, err))
	}
}

func (m *multicastIpv6RoutingTableControllerImpl) GetRouteStats(_ fidl.Context, addresses admin.Ipv6UnicastSourceAndMulticastDestination) (admin.Ipv6RoutingTableControllerGetRouteStatsResult, error) {
	stackAddresses := toStackIPv6UnicastSourceAndMulticastDestination(addresses)
	switch timestamp, err := m.stack.MulticastRouteLastUsedTime(ipv6.ProtocolNumber, stackAddresses); err.(type) {
	case nil:
		var routeStats admin.RouteStats
		// The timestamp comes from the tcpip.Clock, which is backed by the
		// Fuchsia monotonic clock. As a result, the returned timestamp
		// corresponds to a value that all components can understand.
		routeStats.SetLastUsed(timestamp.Sub(tcpip.MonotonicTime{}).Nanoseconds())
		return admin.Ipv6RoutingTableControllerGetRouteStatsResultWithResponse(admin.Ipv6RoutingTableControllerGetRouteStatsResponse{Stats: routeStats}), nil
	case *tcpip.ErrBadAddress:
		return admin.Ipv6RoutingTableControllerGetRouteStatsResultWithErr(admin.Ipv6RoutingTableControllerGetRouteStatsErrorInvalidAddress), nil
	case *tcpip.ErrHostUnreachable:
		return admin.Ipv6RoutingTableControllerGetRouteStatsResultWithErr(admin.Ipv6RoutingTableControllerGetRouteStatsErrorNotFound), nil
	default:
		panic(fmt.Sprintf("m.stack.MulticastRouteLastUsedTime(ipv6.ProtocolNumber, %#v): %s", stackAddresses, err))
	}
}

func (m *multicastIpv6RoutingTableControllerImpl) WatchRoutingEvents(ctx fidl.Context) (uint64, admin.Ipv6UnicastSourceAndMulticastDestination, uint64, admin.RoutingEvent, error) {
	event, numDroppedEvents, err := m.disp.nextMulticastEvent(ctx, func() {
		m.eventSender.OnClose(admin.TableControllerCloseReasonHangingGetError)
	})

	if err != nil {
		return 0, admin.Ipv6UnicastSourceAndMulticastDestination{}, 0, admin.RoutingEvent{}, err
	}

	addresses := admin.Ipv6UnicastSourceAndMulticastDestination{
		UnicastSource:        fidlconv.ToNetIpAddress(event.context.SourceAndDestination.Source).Ipv6,
		MulticastDestination: fidlconv.ToNetIpAddress(event.context.SourceAndDestination.Destination).Ipv6,
	}

	return numDroppedEvents, addresses, uint64(event.context.InputInterface), event.event, nil
}
