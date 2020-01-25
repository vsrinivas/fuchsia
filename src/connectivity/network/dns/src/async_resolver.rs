// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{tcp::DnsTcpStream, udp::DnsUdpSocket, FuchsiaTime},
    fuchsia_async as fasync,
    futures::{task::SpawnExt, Future, FutureExt},
    trust_dns_proto::error::ProtoError,
    trust_dns_resolver::{
        name_server::{
            GenericConnection as Connection, GenericConnectionProvider, RuntimeProvider, Spawn,
        },
        AsyncResolver,
    },
};

/// A new type for `fuchsia_async::EHandle` which implements the
/// `trust_dns_resolver::name_server::Spawn` trait.
#[derive(Clone)]
pub struct Handle(fasync::EHandle);

impl Handle {
    /// Constructs a Handle with an underlying data of `fuchsia_async::EHandle` type.
    pub fn new(handle: fasync::EHandle) -> Self {
        Self(handle)
    }
}

impl Spawn for Handle {
    fn spawn_bg<F>(&mut self, future: F)
    where
        F: Future<Output = Result<(), ProtoError>> + Send + 'static,
    {
        let _join = self.0.spawn(future.map(|_| ()));
    }
}

/// A Fuchsia Runtime type which uses `Handle`, `DnsTcpStream`, `FuchsiaTime`, and `DnsUdpSocket`
/// defined in this crate.
// NOTE: This is an abstracted type used to run trus_dns_resolver::* APIs.
#[derive(Clone)]
pub struct FuchsiaRuntime;

impl RuntimeProvider for FuchsiaRuntime {
    type Handle = Handle;
    type Tcp = DnsTcpStream;
    type Timer = FuchsiaTime;
    type Udp = DnsUdpSocket;
}

/// ConnectionProvider that creates new connections to DNS servers on Fuchsia.
pub type ConnectionProvider = GenericConnectionProvider<FuchsiaRuntime>;

/// Resolver that resolves DNS requests on Fuchsia.
pub type Resolver = AsyncResolver<Connection, ConnectionProvider>;

#[cfg(test)]
mod tests {
    use crate::FuchsiaExec;
    use trust_dns_resolver::config::ResolverConfig;

    use super::*;

    #[test]
    fn test_ip_lookup() {
        use trust_dns_resolver::testing::ip_lookup_test;
        let mut exec = FuchsiaExec::new().expect("failed to create fuchsia executor");
        let handle = Handle::new(exec.get().ehandle());
        ip_lookup_test::<FuchsiaExec, FuchsiaRuntime>(exec, handle)
    }

    #[test]
    fn test_ip_lookup_across_threads() {
        use trust_dns_resolver::testing::ip_lookup_across_threads_test;
        let mut exec = FuchsiaExec::new().expect("failed to create fuchsia executor");
        let handle = Handle::new(exec.get().ehandle());
        ip_lookup_across_threads_test::<FuchsiaExec, FuchsiaRuntime>(exec, handle)
    }

    #[test]
    fn test_localhost_ipv4() {
        use trust_dns_resolver::testing::localhost_ipv4_test;
        let mut exec = FuchsiaExec::new().expect("failed to create fuchsia executor");
        let handle = Handle::new(exec.get().ehandle());
        localhost_ipv4_test::<FuchsiaExec, FuchsiaRuntime>(exec, handle);
    }

    #[test]
    fn test_localhost_ipv6() {
        use trust_dns_resolver::testing::localhost_ipv6_test;
        let mut exec = FuchsiaExec::new().expect("failed to create fuchsia executor");
        let handle = Handle::new(exec.get().ehandle());
        localhost_ipv6_test::<FuchsiaExec, FuchsiaRuntime>(exec, handle);
    }

    #[test]
    fn test_search_ipv4_large_ndots() {
        use trust_dns_resolver::testing::search_ipv4_large_ndots_test;
        let mut exec = FuchsiaExec::new().expect("failed to create fuchsia executor");
        let handle = Handle::new(exec.get().ehandle());
        search_ipv4_large_ndots_test::<FuchsiaExec, FuchsiaRuntime>(exec, handle);
    }

    #[test]
    fn test_search_ipv6_large_ndots() {
        use trust_dns_resolver::testing::search_ipv6_large_ndots_test;
        let mut exec = FuchsiaExec::new().expect("failed to create fuchsia executor");
        let handle = Handle::new(exec.get().ehandle());
        search_ipv6_large_ndots_test::<FuchsiaExec, FuchsiaRuntime>(exec, handle);
    }

    #[test]
    fn test_search_ipv6_name_parse_fails() {
        use trust_dns_resolver::testing::search_ipv6_name_parse_fails_test;
        let mut exec = FuchsiaExec::new().expect("failed to create fuchsia executor");
        let handle = Handle::new(exec.get().ehandle());
        search_ipv6_name_parse_fails_test::<FuchsiaExec, FuchsiaRuntime>(exec, handle);
    }

    // TODO(chunyingw): Make it as a manual test.
    // The test requires Internet connection for testing.
    #[test]
    #[ignore]
    fn test_lookup_google() {
        use trust_dns_resolver::testing::lookup_test;
        let mut exec = FuchsiaExec::new().expect("failed to create fuchsia executor");
        let handle = Handle::new(exec.get().ehandle());
        lookup_test::<FuchsiaExec, FuchsiaRuntime>(ResolverConfig::google(), exec, handle)
    }

    // TODO(chunyingw): Make it as a manual test.
    // The test requires Internet connection for testing.
    #[test]
    #[ignore]
    fn test_lookup_cloudflare() {
        use trust_dns_resolver::testing::lookup_test;
        let mut exec = FuchsiaExec::new().expect("failed to create fuchsia executor");
        let handle = Handle::new(exec.get().ehandle());
        lookup_test::<FuchsiaExec, FuchsiaRuntime>(ResolverConfig::cloudflare(), exec, handle)
    }

    // TODO(chunyingw): Make it as a manual test.
    // The test requires Internet connection for testing.
    #[test]
    #[ignore] // TODO(chunyingw): Make it as a manual test.
              // The test requires Internet connection for testing.
    fn test_lookup_quad9() {
        use trust_dns_resolver::testing::lookup_test;
        let mut exec = FuchsiaExec::new().expect("failed to create fuchsia executor");
        let handle = Handle::new(exec.get().ehandle());
        lookup_test::<FuchsiaExec, FuchsiaRuntime>(ResolverConfig::quad9(), exec, handle)
    }

    // TODO(chunyingw): Make it as a manual test.
    // The test requires Internet connection for testing.
    #[test]
    #[ignore]
    fn test_fqdn() {
        use trust_dns_resolver::testing::fqdn_test;
        let mut exec = FuchsiaExec::new().expect("failed to create fuchsia executor");
        let handle = Handle::new(exec.get().ehandle());
        fqdn_test::<FuchsiaExec, FuchsiaRuntime>(exec, handle);
    }

    // TODO(chunyingw): Make it as a manual test.
    // The test requires Internet connection for testing.
    #[test]
    #[ignore]
    fn test_ndots() {
        use trust_dns_resolver::testing::ndots_test;
        let mut exec = FuchsiaExec::new().expect("failed to create fuchsia executor");
        let handle = Handle::new(exec.get().ehandle());
        ndots_test::<FuchsiaExec, FuchsiaRuntime>(exec, handle);
    }

    // TODO(chunyingw): Make it as a manual test.
    // The test requires Internet connection for testing.
    #[test]
    #[ignore]
    fn test_large_ndots() {
        use trust_dns_resolver::testing::large_ndots_test;
        let mut exec = FuchsiaExec::new().expect("failed to create fuchsia executor");
        let handle = Handle::new(exec.get().ehandle());
        large_ndots_test::<FuchsiaExec, FuchsiaRuntime>(exec, handle);
    }

    // TODO(chunyingw): Make it as a manual test.
    // The test requires Internet connection for testing.
    #[test]
    #[ignore]
    fn test_domain_search() {
        use trust_dns_resolver::testing::domain_search_test;
        let mut exec = FuchsiaExec::new().expect("failed to create fuchsia executor");
        let handle = Handle::new(exec.get().ehandle());
        domain_search_test::<FuchsiaExec, FuchsiaRuntime>(exec, handle);
    }

    // TODO(chunyingw): Make it as a manual test.
    // The test requires Internet connection for testing.
    #[test]
    #[ignore]
    fn test_search_list() {
        use trust_dns_resolver::testing::search_list_test;
        let mut exec = FuchsiaExec::new().expect("failed to create fuchsia executor");
        let handle = Handle::new(exec.get().ehandle());
        search_list_test::<FuchsiaExec, FuchsiaRuntime>(exec, handle);
    }

    // TODO(chunyingw): Make it as a manual test.
    // The test requires Internet connection for testing.
    #[test]
    #[ignore]
    fn test_idna() {
        use trust_dns_resolver::testing::idna_test;
        let mut exec = FuchsiaExec::new().expect("failed to create fuchsia executor");
        let handle = Handle::new(exec.get().ehandle());
        idna_test::<FuchsiaExec, FuchsiaRuntime>(exec, handle);
    }
}
