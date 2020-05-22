// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::dns_server_watcher::DnsServerWatcherEvent;
use fidl_fuchsia_router_config::{RouterAdminRequest, RouterStateRequest};

/// The events that can trigger an action in the event loop.
#[derive(Debug)]
pub enum Event {
    /// A request from the fuchsia.router.config Admin FIDL interface
    FidlRouterAdminEvent(RouterAdminRequest),
    /// A request from the fuchsia.router.config State FIDL interface
    FidlRouterStateEvent(RouterStateRequest),
    /// An event coming from fuchsia.net.stack.
    StackEvent(fidl_fuchsia_net_stack::StackEvent),
    /// An event coming from fuchsia.netstack.
    NetstackEvent(fidl_fuchsia_netstack::NetstackEvent),
    /// An event indicating Insertion/Removal of a port.
    OIR(network_manager_core::oir::OIRInfo),
    /// An event with new DNS servers.
    DnsServerWatcher(DnsServerWatcherEvent),
}

impl From<DnsServerWatcherEvent> for Event {
    fn from(e: DnsServerWatcherEvent) -> Event {
        Event::DnsServerWatcher(e)
    }
}
