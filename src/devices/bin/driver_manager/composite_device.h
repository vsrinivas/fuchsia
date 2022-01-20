// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_COMPOSITE_DEVICE_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_COMPOSITE_DEVICE_H_

#include <fidl/fuchsia.device.manager/cpp/wire.h>
#include <lib/ddk/binding.h>

#include <memory>
#include <string_view>

#include <fbl/array.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/string.h>

#include "driver.h"
#include "metadata.h"

// Forward declaration
class CompositeDevice;
class Coordinator;
class Device;

// Tags used for container membership identification
namespace internal {
struct CdfListTag {};
struct CdfDeviceListTag {};
}  // namespace internal

struct StrProperty {
  std::string key;
  std::variant<uint32_t, std::string, bool> value;
};

// A single device that is part of a composite device.
// TODO(teisenbe): Should this just be an inner-class of CompositeDevice?
class CompositeDeviceFragment
    : public fbl::ContainableBaseClasses<
          fbl::TaggedDoublyLinkedListable<std::unique_ptr<CompositeDeviceFragment>,
                                          internal::CdfListTag,
                                          fbl::NodeOptions::AllowMultiContainerUptr>,
          fbl::TaggedDoublyLinkedListable<CompositeDeviceFragment*, internal::CdfDeviceListTag>> {
 public:
  using ListTag = internal::CdfListTag;
  using DeviceListTag = internal::CdfDeviceListTag;

  CompositeDeviceFragment(CompositeDevice* composite, std::string name, uint32_t index,
                          fbl::Array<const zx_bind_inst_t> bind_rules);

  CompositeDeviceFragment(CompositeDeviceFragment&&) = delete;
  CompositeDeviceFragment& operator=(CompositeDeviceFragment&&) = delete;

  CompositeDeviceFragment(const CompositeDeviceFragment&) = delete;
  CompositeDeviceFragment& operator=(const CompositeDeviceFragment&) = delete;

  ~CompositeDeviceFragment();

  // Attempt to match this fragment against |dev|.  Returns true if the match
  // was successful.
  bool TryMatch(const fbl::RefPtr<Device>& dev) const;

  // Bind this fragment to the given device.
  zx_status_t Bind(const fbl::RefPtr<Device>& dev);

  // Unbind this fragment.
  void Unbind();

  std::string_view name() const { return name_; }
  uint32_t index() const { return index_; }
  CompositeDevice* composite() const { return composite_; }
  // If not nullptr, this fragment has been bound to this device
  const fbl::RefPtr<Device>& bound_device() const { return bound_device_; }

  const fbl::RefPtr<Device>& fragment_device() const { return fragment_device_; }
  // Registers (or unregisters) the fragment device (i.e. an instance of the
  // "fragment" driver) that bound to bound_device().
  void set_fragment_device(fbl::RefPtr<Device> device) { fragment_device_ = std::move(device); }

 private:
  // The CompositeDevice that this is a part of.
  CompositeDevice* const composite_;

  // The name of this fragment within its CompositeDevice.
  const std::string name_;

  // The index of this fragment within its CompositeDevice.
  const uint32_t index_;

  // Bind rules for the fragment.
  const fbl::Array<const zx_bind_inst_t> bind_rules_;

  // If this fragment has been bound to a device, this points to that device.
  fbl::RefPtr<Device> bound_device_ = nullptr;
  // Once the bound device has the fragment driver attach to it, this points
  // to the device managed by the fragment driver.
  fbl::RefPtr<Device> fragment_device_ = nullptr;
};

// A device composed of other devices.
class CompositeDevice : public fbl::DoublyLinkedListable<std::unique_ptr<CompositeDevice>> {
 public:
  // Only public because of make_unique.  You probably want Create().
  CompositeDevice(fbl::String name, fbl::Array<const zx_device_prop_t> properties,
                  fbl::Array<const StrProperty> str_properties, uint32_t fragments_count,
                  uint32_t primary_fragment_index, bool spawn_colocated,
                  fbl::Array<std::unique_ptr<Metadata>> metadata, bool from_driver_index);

  CompositeDevice(CompositeDevice&&) = delete;
  CompositeDevice& operator=(CompositeDevice&&) = delete;

  CompositeDevice(const CompositeDevice&) = delete;
  CompositeDevice& operator=(const CompositeDevice&) = delete;

  ~CompositeDevice();

  static zx_status_t Create(std::string_view name,
                            fuchsia_device_manager::wire::CompositeDeviceDescriptor comp_desc,
                            std::unique_ptr<CompositeDevice>* out);

  static zx_status_t CreateFromDriverIndex(MatchedDriver driver,
                                           std::unique_ptr<CompositeDevice>* out);

  const fbl::String& name() const { return name_; }
  const fbl::Array<const zx_device_prop_t>& properties() const { return properties_; }
  const fbl::Array<const StrProperty>& str_properties() const { return str_properties_; }
  uint32_t fragments_count() const { return fragments_count_; }

  // Returns a reference to the constructed composite device, if it exists.
  fbl::RefPtr<Device> device() const { return device_; }

  // Attempt to match and bind any of the unbound fragments against |dev|.
  zx_status_t TryMatchBindFragments(const fbl::RefPtr<Device>& dev);

  // Bind the fragment with the given index to the specified device
  zx_status_t BindFragment(size_t index, const fbl::RefPtr<Device>& dev);

  // Mark the given fragment as unbound.  Note that since we don't expose
  // this device's fragments in the API, this method can only be invoked by
  // CompositeDeviceFragment
  void UnbindFragment(CompositeDeviceFragment* fragment);

  // Creates the actual device and orchestrates the creation of the composite
  // device in a driver_host.
  // Returns ZX_ERR_SHOULD_WAIT if some fragment is not fully ready (i.e. has
  // either not been matched or the fragment driver that bound to it has not
  // yet published its device).
  zx_status_t TryAssemble();

  // Forget about the composite device that was constructed.  If TryAssemble()
  // is invoked after this, it will reassemble the device.
  void Remove();

  using FragmentList = fbl::TaggedDoublyLinkedList<std::unique_ptr<CompositeDeviceFragment>,
                                                   CompositeDeviceFragment::ListTag>;
  FragmentList& bound_fragments() { return bound_fragments_; }

 private:
  // Returns true if a fragment matches |dev|. Sets |*index_out| will be set to the
  // matching fragment.
  bool IsFragmentMatch(const fbl::RefPtr<Device>& dev, size_t* index_out) const;

  const fbl::String name_;
  const fbl::Array<const zx_device_prop_t> properties_;
  const fbl::Array<const StrProperty> str_properties_;

  const uint32_t fragments_count_;
  const uint32_t primary_fragment_index_;

  const bool spawn_colocated_;
  const fbl::Array<std::unique_ptr<Metadata>> metadata_;

  // Driver index variables. |driver_index_driver_| is set by CreateFromDriverIndex().
  const bool from_driver_index_;
  const Driver* driver_index_driver_;

  FragmentList unbound_fragments_;
  FragmentList bound_fragments_;

  // Once the composite has been assembled, this refers to the constructed
  // device.
  fbl::RefPtr<Device> device_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_COMPOSITE_DEVICE_H_
