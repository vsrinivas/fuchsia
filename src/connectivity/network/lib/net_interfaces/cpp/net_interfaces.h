// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_LIB_NET_INTERFACES_CPP_NET_INTERFACES_H_
#define SRC_CONNECTIVITY_NETWORK_LIB_NET_INTERFACES_CPP_NET_INTERFACES_H_

#include <fuchsia/net/interfaces/cpp/fidl.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <variant>

namespace net::interfaces {
// Move-only wrapper for fuchsia::net::interface::Properties that guarantees
// that its inner |properties_| are valid and complete properties as reported by
// |VerifyCompleteProperties|.
class Properties final {
 public:
  // Creates an |Properties| if |properties| are valid complete
  // properties as reported by |VerifyCompleteProperties|.
  static std::optional<Properties> VerifyAndCreate(fuchsia::net::interfaces::Properties properties);
  Properties(Properties&& interface) noexcept;
  Properties& operator=(Properties&& interface) noexcept;
  ~Properties();

  bool operator==(const Properties& rhs) const;

  // Updates this instance with the values set in |properties|. Fields not set in |properties|
  // retain their previous values. Returns false if the |properties| has a missing or mismatched
  // |id| field, or if any field set in |properties| has an invalid value (e.g. an address that does
  // not actually contain an address).
  //
  // |properties| will be modified such that it contains the previous values of all mutable fields
  // which changed upon return. This is useful when a user needs to know precisely how the addresses
  // field has changed as a result of the update. Note that in particular, the |id| field is
  // immutable and thus will not be present in |properties| upon return.
  bool Update(fuchsia::net::interfaces::Properties* properties);

  // Returns true iff the interface's properties indicate that it is globally routable.
  // This is defined as being an interface that is not loopback, is online, and either has an IPv4
  // address and a default IPv4 route or has a global unicast IPv6 address and a default IPv6 route.
  bool IsGloballyRoutable() const;

  uint64_t id() const { return properties_.id(); }
  const ::std::string& name() const { return properties_.name(); }
  const fuchsia::net::interfaces::DeviceClass& device_class() const {
    return properties_.device_class();
  }
  bool is_loopback() const;
  bool online() const { return properties_.online(); }
  bool has_default_ipv4_route() const { return properties_.has_default_ipv4_route(); }
  bool has_default_ipv6_route() const { return properties_.has_default_ipv6_route(); }
  const ::std::vector<fuchsia::net::interfaces::Address>& addresses() const {
    return properties_.addresses();
  }

 private:
  explicit Properties(fuchsia::net::interfaces::Properties properties);

  fuchsia::net::interfaces::Properties properties_;
};

// Move-only type which holds a collection of interface properties and can be updated with
// |fuchsia::net::interfaces::Event| events.
class PropertiesMap final {
 public:
  enum class UpdateErrorWithIdKind {
    kDuplicateExisting,
    kDuplicateAdded,
    kUnknownChanged,
    kUnknownRemoved,
  };
  template <UpdateErrorWithIdKind T>
  struct UpdateErrorWithId {
    uint64_t id;

    bool operator==(const UpdateErrorWithId<T>& rhs) const { return id == rhs.id; }
  };

  enum class UpdateError {
    kInvalidExisting,
    kInvalidAdded,
    kMissingId,
    kInvalidChanged,
    kInvalidEvent,
  };

  using UpdateErrorVariant =
      std::variant<UpdateError, UpdateErrorWithId<UpdateErrorWithIdKind::kDuplicateExisting>,
                   UpdateErrorWithId<UpdateErrorWithIdKind::kDuplicateAdded>,
                   UpdateErrorWithId<UpdateErrorWithIdKind::kUnknownChanged>,
                   UpdateErrorWithId<UpdateErrorWithIdKind::kUnknownRemoved>>;

  static std::string update_error_get_string(UpdateErrorVariant variant);

  PropertiesMap() noexcept;
  PropertiesMap(PropertiesMap&& interface) noexcept;
  PropertiesMap& operator=(PropertiesMap&& interface) noexcept;
  ~PropertiesMap();

  // Updates this properties map with |event|, returning an optional error.
  fpromise::result<void, UpdateErrorVariant> Update(fuchsia::net::interfaces::Event event);

  const std::unordered_map<uint64_t, Properties>& properties_map() const { return properties_map_; }

 private:
  std::unordered_map<uint64_t, Properties> properties_map_;

  // Helper type for visitor in |update_error_get_string|.
  template <class... Ts>
  struct UpdateErrorVisitor : Ts... {
    using Ts::operator()...;
  };
  template <class... Ts>
  UpdateErrorVisitor(Ts...) -> UpdateErrorVisitor<Ts...>;
};

}  // namespace net::interfaces

#endif  // SRC_CONNECTIVITY_NETWORK_LIB_NET_INTERFACES_CPP_NET_INTERFACES_H_
