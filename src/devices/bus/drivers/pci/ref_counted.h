// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BUS_DRIVERS_PCI_REF_COUNTED_H_
#define SRC_DEVICES_BUS_DRIVERS_PCI_REF_COUNTED_H_

#include <assert.h>
#include <zircon/compiler.h>

#include <fbl/ref_counted.h>

/**
 * Notes on class hierarchy and RefCounting
 *
 * The PCI/PCIe device class hierarchy consists of 3 main types of object.
 *
 * ++ Root
 * A root of a PCI/PCIe device tree.  Roots do not have standard config
 * registers, but do have a collection of downstream Device children.  In
 * addition, PCIe roots (as opposed to plain PCI roots) have a special set of
 * registers called the "root complex control block".  The PCIe bus driver
 * supports systems which have multiple roots and maintains a collection of
 * roots which were registered by the system.
 *
 * ++ Device
 * The actual devices in the PCIe hierarchy.  Devices have a set of PCI/PCIe
 * config registers, can allocate apertures in Memory and I/O space, can map
 * interrupts, and can have drivers attached to them.  All devices are the child
 * of either a Root or a Bridge, but have no children themselves.
 *
 * ++ Bridge
 * Bridges are devices with children.  Because they are devices, bridges
 * have config, can map registers, deliver interrupts, have drivers bound to
 * them, and are always the child of either a Root or another Bridge.
 * In addition (unlike Devices), Bridges have roots.
 *
 * In order to avoid code duplication, two classes have been introduced and
 * Bridge makes limited use of multiple inheritance in order to be a device
 * with children, while not being a root.  The classes introduced are...
 *
 * ++ UpstreamNode
 * A UpstreamNode is an object which can have Device children.  Roots
 * and Bridges are both are upstream nodes.  Devices hold a reference to their
 * upstream node, without needing to understand whether they are downstream of a
 * root or a bridge.
 *
 * ++ DeviceImpl
 * A small class used to deal with some of the ref counting issues which arise
 * from this arrangement.  More on this later.
 *
 * A simple diagram of the class hierarchy looks like this.
 *
 *            +---------------+       +--------+
 *            | Upstream Node |       | Device |
 *            +---------------+       +--------+
 *              |    |                  |   |
 * +------+     |    |    +--------+    |   |
 * | Root | <---/    \--->| Bridge |<---/   |
 * +------+               +--------+        |
 *                                          |
 *                    +------------+        |
 *                    | DeviceImpl |<-------/
 *                    +------------+
 *
 * ==== RefCounting ====
 *
 * Object lifetimes are managed using fbl::RefPtr<>.  Because of this, all
 * objects must provide an implementation of AddRef/Release/Adopt which is
 * compatible with fbl::RefPtr<>.  The bus driver holds RefPtr<Root>s,
 * UpstreamNodes hold RefPtr<Device>s and Devices hold RefPtr<UpstreamNode>s
 * back to their owners.
 *
 * RefPtr to both Devices and UpstreamNodes exist in the system, so both object
 * must expose an interface to reference counting which is compatible with
 * fbl::RefPtr<>.  Because a Bridge is both an UpstreamNode and a Device,
 * simply having Device and UpstreamNode derive from fbl::RefCounted<> (which
 * would be standard practice) will not work.  The Bridge object which results
 * from this would have two different ref counts which would end up being
 * manipulated independently.
 *
 * A simple solution to this would be to have all of the objects in the system
 * inherit virtually from an implementation of fbl::RefCounted<>.
 * Unfortunately, the power that be strictly prohibit the use of virtual
 * inheritance in this codebase.  Because of this, a different solution needs to
 * be provided.  Here is how this system works.
 *
 * Two macros have been defined (below).  One or the other of them *must* be
 * included in the public section of every class involved in this hierarchy.
 *
 * ++ PCI_REQUIRE_REFCOUNTED
 * Any class which is a base class of any other class in this hierarchy *must*
 * include this macro.  It defines pure virtual AddRef/Release/Adopt members
 * whose signatures are compatible with fbl::RefPtr.  This requires an
 * implementation to exist in derived classes, redirects ref counting behavior
 * to this implementation, and prevents accidental instantiation of the base
 * class.  UpstreamNode and Device REQUIRE_REFCOUNTED.
 *
 * ++ PCI_IMPLEMENT_REFCOUNTED
 * Any class which is a child of one or more of the base classes *must* include
 * this macro.  This macro wraps an implementation of fbl::RefCounted<> (so
 * that code duplication is minimized, and atomic ref count access is consistent
 * throughout the system), and marks the virtual AddRef/Release/Adopt methods as
 * being final, which helps to prevent a different implementation accidentally
 * being added to the class hierarchy.  Root, Bridge and DeviceImpl
 * IMPLEMENT_REFCOUNTED.
 *
 * Finally, coming back to the issue of DeviceImpl...
 * Because Device is a base class for Bridge, it cannot IMPLEMENT_REFCOUNTED
 * itself.  Instead, it must REQUIRE_REFCOUNTED (redirecting ref counting
 * behavior to the Bridge implementation).  This means that Device can no longer
 * be instantiated (because it is abstract).  DeviceImpl is a small class which
 * does nothing but derive from Device and implement the ref counting.  Its
 * implementation exists inside of an anonymous namespace inside
 * pcie_device.cpp so none of the rest of the system ever sees it.
 * Device::Create returns a fbl::RefPtr<Device> which actually points
 * to an instance of DeviceImpl created by the Create method.
 */

#define PCI_REQUIRE_REFCOUNTED \
  virtual void Adopt() = 0;    \
  virtual void AddRef() = 0;   \
  virtual bool Release() = 0

#define PCI_IMPLEMENT_REFCOUNTED                                                  \
 private:                                                                         \
  fbl::RefCounted<void> ref_count_impl_;                                          \
                                                                                  \
 public:                                                                          \
  void Adopt() final { ref_count_impl_.Adopt(); }                                 \
  void AddRef() final { ref_count_impl_.AddRef(); }                               \
  bool Release() final __WARN_UNUSED_RESULT { return ref_count_impl_.Release(); } \
  using __force_semicolon = int

#endif  // SRC_DEVICES_BUS_DRIVERS_PCI_REF_COUNTED_H_
