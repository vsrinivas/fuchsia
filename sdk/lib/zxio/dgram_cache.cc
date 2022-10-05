// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.posix.socket/cpp/wire.h>
#include <lib/fit/result.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/handle.h>
#include <lib/zx/time.h>
#include <lib/zxio/cpp/dgram_cache.h>
#include <lib/zxio/cpp/socket_address.h>
#include <zircon/types.h>

#include <optional>
#include <utility>
#include <vector>

#include "sdk/lib/zxio/hash.h"

namespace fnet = fuchsia_net;
namespace fsocket = fuchsia_posix_socket;

using fuchsia_posix_socket::wire::CmsgRequests;

RequestedCmsgSet::RequestedCmsgSet(
    const fsocket::wire::DatagramSocketRecvMsgPostflightResponse& response) {
  if (response.has_requests()) {
    requests_ = response.requests();
  }
  if (response.has_timestamp()) {
    so_timestamp_filter_ = response.timestamp();
  } else {
    so_timestamp_filter_ = fsocket::wire::TimestampOption::kDisabled;
  }
}

std::optional<fsocket::wire::TimestampOption> RequestedCmsgSet::so_timestamp() const {
  return so_timestamp_filter_;
}

bool RequestedCmsgSet::ip_tos() const {
  return static_cast<bool>(requests_ & CmsgRequests::kIpTos);
}

bool RequestedCmsgSet::ip_ttl() const {
  return static_cast<bool>(requests_ & CmsgRequests::kIpTtl);
}

bool RequestedCmsgSet::ipv6_tclass() const {
  return static_cast<bool>(requests_ & CmsgRequests::kIpv6Tclass);
}

bool RequestedCmsgSet::ipv6_hoplimit() const {
  return static_cast<bool>(requests_ & CmsgRequests::kIpv6Hoplimit);
}

bool RequestedCmsgSet::ipv6_pktinfo() const {
  return static_cast<bool>(requests_ & CmsgRequests::kIpv6Pktinfo);
}

// TODO(https://fxbug.dev/97260): Implement cache eviction strategy to avoid unbounded cache
// growth.
using RequestedCmsgResult = fit::result<ErrOrOutCode, std::optional<RequestedCmsgSet>>;
RequestedCmsgResult RequestedCmsgCache::Get(zx_wait_item_t err_wait_item,
                                            bool get_requested_cmsg_set,
                                            fidl::WireSyncClient<fsocket::DatagramSocket>& client) {
  // TODO(https://fxbug.dev/103653): Circumvent fast-path pessimization caused by lock
  // contention between multiple fast paths.
  std::lock_guard lock(lock_);

  constexpr size_t MAX_WAIT_ITEMS = 2;
  zx_wait_item_t wait_items[MAX_WAIT_ITEMS];
  constexpr uint32_t ERR_WAIT_ITEM_IDX = 0;
  wait_items[ERR_WAIT_ITEM_IDX] = err_wait_item;
  std::optional<size_t> cmsg_idx;
  while (true) {
    uint32_t num_wait_items = ERR_WAIT_ITEM_IDX + 1;

    if (get_requested_cmsg_set && cache_.has_value()) {
      wait_items[num_wait_items] = {
          .handle = cache_.value().validity.get(),
          .waitfor = ZX_EVENTPAIR_PEER_CLOSED,
      };
      cmsg_idx = num_wait_items;
      num_wait_items++;
    }

    zx_status_t status =
        zx::handle::wait_many(wait_items, num_wait_items, zx::time::infinite_past());

    switch (status) {
      case ZX_OK: {
        const zx_wait_item_t& err_wait_item_ref = wait_items[ERR_WAIT_ITEM_IDX];
        if (err_wait_item_ref.pending & err_wait_item_ref.waitfor) {
          std::optional err = GetErrorWithClient(client);
          if (err.has_value()) {
            return fit::error(err.value());
          }
          continue;
        }
        ZX_ASSERT_MSG(cmsg_idx.has_value(), "wait_many({{.pending = %d, .waitfor = %d}}) == ZX_OK",
                      err_wait_item_ref.pending, err_wait_item_ref.waitfor);
        const zx_wait_item_t& cmsg_wait_item_ref = wait_items[cmsg_idx.value()];
        ZX_ASSERT_MSG(cmsg_wait_item_ref.pending & cmsg_wait_item_ref.waitfor,
                      "wait_many({{.pending = %d, .waitfor = %d}, {.pending = %d, .waitfor = "
                      "%d}}) == ZX_OK",
                      err_wait_item_ref.pending, err_wait_item_ref.waitfor,
                      cmsg_wait_item_ref.pending, cmsg_wait_item_ref.waitfor);
      } break;
      case ZX_ERR_TIMED_OUT: {
        if (!get_requested_cmsg_set) {
          return fit::ok(std::nullopt);
        }
        if (cache_.has_value()) {
          return fit::ok(cache_.value().requested_cmsg_set);
        }
      } break;
      default:
        ErrOrOutCode err = zx::error(status);
        return fit::error(err);
    }

    const fidl::WireResult response = client->RecvMsgPostflight();
    if (!response.ok()) {
      ErrOrOutCode err = zx::error(response.status());
      return fit::error(err);
    }
    const auto& result = response.value();
    if (result.is_error()) {
      return fit::error(zx::ok(static_cast<int16_t>(result.error_value())));
    }
    fsocket::wire::DatagramSocketRecvMsgPostflightResponse& response_inner = *result.value();
    if (!response_inner.has_validity()) {
      return fit::error(zx::ok(static_cast<int16_t>(EIO)));
    }
    cache_ = Value{
        .validity = std::move(response_inner.validity()),
        .requested_cmsg_set = RequestedCmsgSet(response_inner),
    };
  }
}

// TODO(https://fxbug.dev/7958): remove this custom implementation when FIDL
// wire types support deep equality.
bool RouteCache::Key::operator==(const RouteCache::Key& o) const {
  if (remote_addr != o.remote_addr) {
    return false;
  }
  if (local_iface_and_addr.has_value() != o.local_iface_and_addr.has_value()) {
    return false;
  }
  if (!local_iface_and_addr.has_value()) {
    return true;
  }
  const auto& [iface, addr] = local_iface_and_addr.value();
  const auto& [other_iface, other_addr] = o.local_iface_and_addr.value();
  if (iface != other_iface) {
    return false;
  }
  return addr.addr == other_addr.addr;
}

size_t RouteCache::KeyHasher::operator()(const Key& k) const {
  size_t h = k.remote_addr.hash();
  if (k.local_iface_and_addr.has_value()) {
    const auto& [iface, addr] = k.local_iface_and_addr.value();
    hash_combine(h, iface);
    for (const auto& addr_bits : addr.addr) {
      hash_combine(h, addr_bits);
    }
  }
  return h;
}

std::list<RouteCache::Key>::iterator RouteCache::LruAddToFrontLocked(const Key& k) {
  lru_.push_front(k);
  return lru_.begin();
}

using RouteCacheResult = fit::result<ErrOrOutCode, uint32_t>;
RouteCacheResult RouteCache::Get(
    std::optional<SocketAddress>& remote_addr,
    const std::optional<std::pair<uint64_t, fuchsia_net::wire::Ipv6Address>>& local_iface_and_addr,
    const zx_wait_item_t& err_wait_item, fidl::WireSyncClient<fsocket::DatagramSocket>& client) {
  // TODO(https://fxbug.dev/103653): Circumvent fast-path pessimization caused by lock
  // contention 1) between multiple fast paths and 2) between fast path and slow path.
  std::lock_guard lock(lock_);

  zx_wait_item_t wait_items[ZX_WAIT_MANY_MAX_ITEMS];
  constexpr uint32_t ERR_WAIT_ITEM_IDX = 0;
  wait_items[ERR_WAIT_ITEM_IDX] = err_wait_item;

  while (true) {
    std::optional<uint32_t> maximum_size;
    uint32_t num_wait_items = ERR_WAIT_ITEM_IDX + 1;
    const std::optional<SocketAddress>& addr_to_lookup =
        remote_addr.has_value() ? remote_addr : connected_;

    // NOTE: `addr_to_lookup` might not have a value if we're looking up the
    // connected addr for the first time. We still proceed with the syscall
    // to check for errors in that case (since the socket might have been
    // connected by another process).
    //
    // TODO(https://fxbug.dev/103655): Test errors are returned when connected
    // addr looked up for the first time.
    if (addr_to_lookup.has_value()) {
      const Key key = {
          .remote_addr = addr_to_lookup.value(),
          .local_iface_and_addr = local_iface_and_addr,
      };
      if (auto it = cache_.find(key); it != cache_.end()) {
        Value& value = it->second;

        // Mark this entry in the cache as the most recently-used.
        lru_.erase(value.lru);
        value.lru = LruAddToFrontLocked(key);

        ZX_ASSERT_MSG(value.eventpairs.size() + 1 <= ZX_WAIT_MANY_MAX_ITEMS,
                      "number of wait_items (%lu) exceeds maximum allowed (%zu)",
                      value.eventpairs.size() + 1, ZX_WAIT_MANY_MAX_ITEMS);
        for (const zx::eventpair& eventpair : value.eventpairs) {
          wait_items[num_wait_items] = {
              .handle = eventpair.get(),
              .waitfor = ZX_EVENTPAIR_PEER_CLOSED,
          };
          num_wait_items++;
        }
        maximum_size = value.maximum_size;
      }
    }

    zx_status_t status =
        zx::handle::wait_many(wait_items, num_wait_items, zx::time::infinite_past());

    switch (status) {
      case ZX_OK: {
        if (wait_items[ERR_WAIT_ITEM_IDX].pending & wait_items[ERR_WAIT_ITEM_IDX].waitfor) {
          std::optional err = GetErrorWithClient(client);
          if (err.has_value()) {
            return fit::error(err.value());
          }
          continue;
        }
      } break;
      case ZX_ERR_TIMED_OUT: {
        if (maximum_size.has_value()) {
          return fit::success(maximum_size.value());
        }
      } break;
      default:
        ErrOrOutCode err = zx::error(status);
        return fit::error(err);
    }

    // TODO(https://fxbug.dev/103740): Avoid allocating into this arena.
    fidl::Arena alloc;
    const fidl::WireResult response = [&client, &alloc, &remote_addr, &local_iface_and_addr]() {
      fidl::WireTableBuilder request_builder =
          fsocket::wire::DatagramSocketSendMsgPreflightRequest::Builder(alloc);
      if (remote_addr.has_value()) {
        remote_addr.value().WithFIDL(
            [&request_builder](fnet::wire::SocketAddress address) { request_builder.to(address); });
      }
      if (local_iface_and_addr.has_value()) {
        const auto& [iface, addr] = local_iface_and_addr.value();
        request_builder.ipv6_pktinfo(fsocket::wire::Ipv6PktInfoSendControlData{
            .iface = iface,
            .local_addr = addr,
        });
      }
      return client->SendMsgPreflight(request_builder.Build());
    }();
    if (!response.ok()) {
      ErrOrOutCode err = zx::error(response.status());
      return fit::error(err);
    }
    const auto& result = response.value();
    if (result.is_error()) {
      return fit::error(zx::ok(static_cast<int16_t>(result.error_value())));
    }
    fsocket::wire::DatagramSocketSendMsgPreflightResponse& res = *result.value();

    std::optional<SocketAddress> returned_addr;
    if (!remote_addr.has_value()) {
      if (res.has_to()) {
        returned_addr = SocketAddress::FromFidl(res.to());
      }
    }
    const std::optional<SocketAddress>& addr_to_store =
        remote_addr.has_value() ? remote_addr : returned_addr;

    if (!addr_to_store.has_value()) {
      return fit::error(zx::ok(static_cast<int16_t>(EIO)));
    }

    if (!res.has_maximum_size() || !res.has_validity()) {
      return fit::error(zx::ok(static_cast<int16_t>(EIO)));
    }

    std::vector<zx::eventpair> eventpairs;
    eventpairs.reserve(res.validity().count());
    std::move(res.validity().begin(), res.validity().end(), std::back_inserter(eventpairs));

    // Remove least-recently-used element if cache is at capacity.
    if (cache_.size() == kMaxEntries) {
      const Key& k = lru_.back();
      size_t removed = cache_.erase(k);
      ZX_ASSERT_MSG(removed == 1,
                    "tried to remove least-recently-used item from route cache; removed %zu items",
                    removed);
      lru_.pop_back();
    }

    const Key key = {
        .remote_addr = addr_to_store.value(),
        .local_iface_and_addr = local_iface_and_addr,
    };
    cache_[key] = {
        .eventpairs = std::move(eventpairs),
        .maximum_size = res.maximum_size(),
        .lru = LruAddToFrontLocked(key),
    };

    if (!remote_addr.has_value()) {
      connected_ = addr_to_store.value();
    }
  }
}

std::optional<ErrOrOutCode> GetErrorWithClient(
    fidl::WireSyncClient<fuchsia_posix_socket::DatagramSocket>& client) {
  const fidl::WireResult response = client->GetError();
  if (!response.ok()) {
    return zx::error(response.status());
  }
  const auto& result = response.value();
  if (result.is_error()) {
    return zx::ok(static_cast<int16_t>(result.error_value()));
  }
  return std::nullopt;
}
