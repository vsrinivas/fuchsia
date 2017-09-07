// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <assert.h>
#include <magenta/compiler.h>
#include <fbl/ref_counted.h>

/**
 * Notes on class hierarchy and RefCounting
 *
 * The PCI/PCIe device class hierarchy consists of 3 main types of object.
 *
 * ++ PcieRoot
 * A root of a PCI/PCIe device tree.  Roots do not have standard config
 * registers, but do have a collection of downstream PcieDevice children.  In
 * addition, PCIe roots (as opposed to plain PCI roots) have a special set of
 * registers called the "root complex control block".  The PCIe bus driver
 * supports systems which have multiple roots and maintains a collection of
 * roots which were registered by the system.
 *
 * ++ PcieDevice
 * The actual devices in the PCIe hierarchy.  Devices have a set of PCI/PCIe
 * config registers, can allocate apertures in Memory and I/O space, can map
 * interrupts, and can have drivers attached to them.  All devices are the child
 * of either a PcieRoot or a PcieBridge, but have no children themselves.
 *
 * ++ PcieBridge
 * PcieBridges are devices with children.  Because they are devices, bridges
 * have config, can map registers, deliver interrupts, have drivers bound to
 * them, and are always the child of either a PcieRoot or another PcieBridge.
 * In addition (unlike PcieDevices), Bridges have roots.
 *
 * In order to avoid code duplication, two classes have been introduced and
 * PcieBridge makes limited use of multiple inheritance in order to be a device
 * with children, while not being a root.  The classes introduced are...
 *
 * ++ PcieUpstreamNode
 * A PcieUpstreamNode is an object which can have PcieDevice children.  Roots
 * and bridges are both are upstream nodes.  Devices hold a reference to their
 * upstream node, without needing to understand whether they are downstream of a
 * root or a bridge.
 *
 * ++ PcieDeviceImpl
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
 * ++ PCIE_REQUIRE_REFCOUNTED
 * Any class which is a base class of any other class in this hierarchy *must*
 * include this macro.  It defines pure virtual AddRef/Release/Adopt members
 * whose signatures are compatible with fbl::RefPtr.  This requires an
 * implementation to exist in derived classes, redirects ref counting behavior
 * to this implementation, and prevents accidental instantiation of the base
 * class.  UpstreamNode and Device REQUIRE_REFCOUNTED.
 *
 * ++ PCIE_IMPLEMENT_REFCOUNTED
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
 * PcieDevice::Create returns a fbl::RefPtr<PcieDevice> which actually points
 * to an instance of DeviceImpl created by the Create method.
 */

#if (DEBUG_ASSERT_IMPLEMENTED)
#define __PCIE_REQUIRE_ADOPT virtual void Adopt() = 0
#else   // if (DEBUG_ASSERT_IMPLEMENTED)
#define __PCIE_REQUIRE_ADOPT \
     inline void Adopt() { } \
    using __force_semicolon = int
#endif  // if (DEBUG_ASSERT_IMPLEMENTED)

#define PCIE_REQUIRE_REFCOUNTED \
    __PCIE_REQUIRE_ADOPT; \
    virtual void AddRef()  = 0; \
    virtual bool Release() = 0 \

#if (DEBUG_ASSERT_IMPLEMENTED)
#define __PCIE_IMPLEMENT_ADOPT void Adopt() final { ref_count_impl_.Adopt(); }
#else   // if (DEBUG_ASSERT_IMPLEMENTED)
#define __PCIE_IMPLEMENT_ADOPT
#endif  // if (DEBUG_ASSERT_IMPLEMENTED)

#define PCIE_IMPLEMENT_REFCOUNTED \
private: \
    fbl::RefCounted<void> ref_count_impl_; \
public: \
    __PCIE_IMPLEMENT_ADOPT; \
    void AddRef() final { ref_count_impl_.AddRef(); } \
    bool Release() final __WARN_UNUSED_RESULT { return ref_count_impl_.Release(); } \
    using __force_semicolon = int
