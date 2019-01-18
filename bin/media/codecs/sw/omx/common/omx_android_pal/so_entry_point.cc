// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "so_entry_point.h"

// SoftOMXComponent.h assumes that <log/log.h> is included first.
#include <log/log.h>
#include <media/stagefright/omx/SoftOMXComponent.h>

#include <lib/fit/defer.h>

// We intentionally don't have a "using namespace android;" because this is an
// adapter layer and we want to make clear what's raw OMX and what's android
// stuff that we're hiding from the caller of the entry point defined at the
// bottom of this file.

// In android sources, the per-OMX-codec common entry point signature isn't in
// any header file, so we just declare it here.  We're using this symbol locally
// within each per-codec binary we build for Fuchsia, and wrapping it with an
// extern "C" shared_library entry point that doesn't return a C++ object.  Only
// the latter is exported from the per-android-codec fuchsia shared lib.
android::SoftOMXComponent *createSoftOMXComponent(
    const char *name, const OMX_CALLBACKTYPE *callbacks, OMX_PTR appData,
    OMX_COMPONENTTYPE **component);

namespace {

// A pointer to this function gets used as an OMX_COMPONENTTYPE.ComponentDeInit.
OMX_ERRORTYPE ComponentDeInit(OMX_IN OMX_HANDLETYPE hComponent) {
  auto *me = (android::SoftOMXComponent *)((OMX_COMPONENTTYPE *)hComponent)
                 ->pComponentPrivate;
  me->prepareForDestruction();
  me->decStrong(reinterpret_cast<void *>(ComponentDeInit));
  // It's important that by this point any threads that were created by
  // SimpleSoftOMXComponent or by the lower-layer codec core (if any) are
  // totally done running any code of the present shared library, as the caller
  // of this function will _un-load the code_ of this shared library.
  return OMX_ErrorNone;
}

}  // namespace

extern "C" {
// This interface is not meant to be Fuchsia-wide for SW codecs.  For that, see
// the Codec FIDL interface defined elsewhere.  This commonality of interface
// here is just for building and loading various SW codecs from the android
// sources.
//
// Sets *component to nullptr if create fails, or to non-nullptr if create
// succeeds.
void entrypoint_createSoftOMXComponent(const char *name,
                                       const OMX_CALLBACKTYPE *callbacks,
                                       OMX_PTR appData,
                                       OMX_COMPONENTTYPE **component) {
  // default to reporting failure uness we get far enough
  *component = nullptr;
  // We use the android::sp to ensure that every path from here forward will
  // have at least one strong reference on the SoftOMXComponent (added here if
  // not nullptr), including error paths.
  android::sp<android::SoftOMXComponent> component_cpp =
      createSoftOMXComponent(name, callbacks, appData, component);
  if (!component_cpp) {
    // TODO: can we log an error somewhere and/or return richer error info from
    // a shared_library entry point in Fuchsia-standard ways?

    // assert that we are reporting failure:
    assert(!(*component));
    return;
  }
  // Unfortunately the android code doesn't take advantage of
  // RefBase::onLastStrongRef(), and doesn't seem worth making a wrapper that
  // does just for the benefit of this source file.
  //
  // unless cancelled
  auto pfd = fit::defer(
      [&component_cpp]() { component_cpp->prepareForDestruction(); });
  if (OMX_ErrorNone != component_cpp->initCheck()) {
    assert(!(*component));
    return;
  }
  if (static_cast<OMX_PTR>(component_cpp.get()) !=
      (*component)->pComponentPrivate) {
    // The android code changed to no longer stash SoftOMXComponent* where this
    // code expects.  At the moment there doesn't seem to be any good way to
    // wrap more thoroughly that doesn't also risk getting broken by android
    // changes, so if the stashing has changed in android code, fail the create.

    // assert that we are reporting failure:
    assert(!(*component));
    // ~pfd
    // ~component_cpp
    return;
  }

  if ((*component)->ComponentDeInit) {
    // The android code has changed to fill out this function pointer.  Without
    // a more thourough wrapping, which would itself be subject to breakage by
    // android changes that add more function pointers (to callbacks and/or to
    // component), we have no great place to stash the value of ComponentDeInit.
    // An alternative would be for the present entry point to fill out a wrapper
    // of OMX_COMPONENTTYPE that just points to an OMX_COMPONENTTYPE, but the
    // benefit/cost of that doesn't seem high enough, at least for now.  So if
    // android code changed to start using this function pointer, fail the
    // create.

    // assert that we are reporting failure:
    assert(!(*component));
    // ~pfd
    // ~component_cpp
    return;
  }

  // This ComponentDeInit will call prepareForDestruction().
  (*component)->ComponentDeInit = ComponentDeInit;
  // Don't call prepareForDestruction() during ~pfd.
  pfd.cancel();

  // Prevent ~component_cpp from deleting the codec.  This ref will be removed
  // by ComponentDeInit, so may as well use that as the void* on the ref.
  component_cpp->incStrong(reinterpret_cast<void *>(ComponentDeInit));

  // The non-use of setLibHandle() and libHandle() is intentional, since the
  // loading and un-loading of the shared library is handled in a layer above
  // that doesn't see SoftOMXComponent.

  return;
}

// This function is only used when linked as a static lib, for debug-cycle
// purposes only.
void direct_createSoftOMXComponent(const char *name,
                                   const OMX_CALLBACKTYPE *callbacks,
                                   OMX_PTR appData,
                                   OMX_COMPONENTTYPE **component) {
  entrypoint_createSoftOMXComponent(name, callbacks, appData, component);
}

}  // extern "C"
