// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::inspect_util, fuchsia_inspect::Node, fuchsia_url::pkg_url::PkgUrl, fuchsia_zircon as zx,
};

fn now_monotonic_nanos() -> i64 {
    zx::Time::get(zx::ClockId::Monotonic).into_nanos()
}

/// Wraps the Inspect state of package resolves.
pub struct ResolverService {
    /// How many times the resolver service has fallen back to the
    /// cache package set due to a remote repository returning NOT_FOUND.
    /// TODO(fxbug.dev/50764): remove this stat when we remove this cache fallback behavior.
    cache_fallbacks_due_to_not_found: inspect_util::Counter,
    active_package_resolves: Node,
    _node: Node,
}

impl ResolverService {
    /// Make a `ResolverService` from an Inspect `Node`.
    pub fn from_node(node: Node) -> Self {
        Self {
            cache_fallbacks_due_to_not_found: inspect_util::Counter::new(
                &node,
                "cache_fallbacks_due_to_not_found",
            ),
            active_package_resolves: node.create_child("active_package_resolves"),
            _node: node,
        }
    }

    /// Increment the count of package resolves that have fallen back to cache packages due to a
    /// remote repository returning NOT_FOUND. This fallback behavior will be removed
    /// TODO(fxbug.dev/50764).
    pub fn cache_fallback_due_to_not_found(&self) {
        self.cache_fallbacks_due_to_not_found.increment();
    }

    /// Add a package to the list of active resolves.
    pub fn resolve(&self, original_url: &PkgUrl) -> Package {
        let node = self.active_package_resolves.create_child(original_url.to_string());
        node.record_int("resolve_ts", now_monotonic_nanos());
        Package { node }
    }
}

/// A package that is actively being resolved.
pub struct Package {
    node: Node,
}

impl Package {
    /// Export the package's rewritten url.
    pub fn rewritten_url(self, rewritten_url: &PkgUrl) -> PackageWithRewrittenUrl {
        self.node.record_string("rewritten_url", rewritten_url.to_string());
        PackageWithRewrittenUrl { _node: self.node }
    }
}

/// A package with a rewritten url that is actively being resolved.
pub struct PackageWithRewrittenUrl {
    _node: Node,
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fuchsia_inspect::{assert_inspect_tree, testing::AnyProperty, Inspector},
    };

    #[test]
    fn package_state_progression() {
        let inspector = Inspector::new();

        let resolver_service =
            ResolverService::from_node(inspector.root().create_child("resolver_service"));
        assert_inspect_tree!(
            inspector,
            root: {
                resolver_service: contains {
                    active_package_resolves: {}
                }
            }
        );

        let package = resolver_service.resolve(&"fuchsia-pkg://example.org/name".parse().unwrap());
        assert_inspect_tree!(
            inspector,
            root: {
                resolver_service: contains {
                    active_package_resolves: {
                        "fuchsia-pkg://example.org/name": {
                            resolve_ts: AnyProperty,
                        }
                    }
                }
            }
        );

        let _package =
            package.rewritten_url(&"fuchsia-pkg://rewritten.example.org/name".parse().unwrap());
        assert_inspect_tree!(
            inspector,
            root: {
                resolver_service: contains {
                    active_package_resolves: {
                        "fuchsia-pkg://example.org/name": {
                            resolve_ts: AnyProperty,
                            rewritten_url: "fuchsia-pkg://rewritten.example.org/name",
                        }
                    }
                }
            }
        );
    }

    #[test]
    fn concurrent_resolves() {
        let inspector = Inspector::new();
        let resolver_service =
            ResolverService::from_node(inspector.root().create_child("resolver_service"));

        let _package0 =
            resolver_service.resolve(&"fuchsia-pkg://example.org/name".parse().unwrap());
        let _package1 =
            resolver_service.resolve(&"fuchsia-pkg://example.org/other".parse().unwrap());
        assert_inspect_tree!(
            inspector,
            root: {
                resolver_service: contains {
                    active_package_resolves: {
                        "fuchsia-pkg://example.org/name": contains {},
                        "fuchsia-pkg://example.org/other": contains {}
                    }
                }
            }
        );
    }

    #[test]
    fn cache_fallback_due_to_not_found_increments() {
        let inspector = Inspector::new();

        let resolver_service =
            ResolverService::from_node(inspector.root().create_child("resolver_service"));
        assert_inspect_tree!(
            inspector,
            root: {
                resolver_service: contains {
                    cache_fallbacks_due_to_not_found: 0u64,
                }
            }
        );

        resolver_service.cache_fallback_due_to_not_found();
        assert_inspect_tree!(
            inspector,
            root: {
                resolver_service: contains {
                    cache_fallbacks_due_to_not_found: 1u64,
                }
            }
        );
    }
}
