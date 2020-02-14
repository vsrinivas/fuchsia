// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef GENERIC_PLATFORM_MANAGER_IMPL_FUCHSIA_IPP
#define GENERIC_PLATFORM_MANAGER_IMPL_FUCHSIA_IPP

#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/PlatformManager.h>
#include "generic_platform_manager_impl_fuchsia.h"

// Include the non-inline definitions for the GenericPlatformManagerImpl<> template,
// from which the GenericPlatformManagerImpl_Fuchsia<> template inherits.
#include <Weave/DeviceLayer/internal/GenericPlatformManagerImpl.ipp>


namespace nl {
namespace Weave {
namespace DeviceLayer {
namespace Internal {

template<class ImplClass>
WEAVE_ERROR GenericPlatformManagerImpl_Fuchsia<ImplClass>::_InitWeaveStack(void)
{
    WEAVE_ERROR err = WEAVE_NO_ERROR;

    // Call up to the base class _InitWeaveStack() to perform the bulk of the initialization.
    err = GenericPlatformManagerImpl<ImplClass>::_InitWeaveStack();
    SuccessOrExit(err);

exit:
    return err;
}

template<class ImplClass>
void GenericPlatformManagerImpl_Fuchsia<ImplClass>::_LockWeaveStack(void) {}

template<class ImplClass>
bool GenericPlatformManagerImpl_Fuchsia<ImplClass>::_TryLockWeaveStack(void)
{
    return true;
}

template<class ImplClass>
void GenericPlatformManagerImpl_Fuchsia<ImplClass>::_UnlockWeaveStack(void) {}

template<class ImplClass>
void GenericPlatformManagerImpl_Fuchsia<ImplClass>::_PostEvent(const WeaveDeviceEvent * event) {}

template<class ImplClass>
void GenericPlatformManagerImpl_Fuchsia<ImplClass>::_RunEventLoop(void) {}

template<class ImplClass>
WEAVE_ERROR GenericPlatformManagerImpl_Fuchsia<ImplClass>::_StartEventLoopTask(void) {
    return WEAVE_NO_ERROR;
}

template<class ImplClass>
void GenericPlatformManagerImpl_Fuchsia<ImplClass>::EventLoopTaskMain(void * arg) {}

template<class ImplClass>
WEAVE_ERROR GenericPlatformManagerImpl_Fuchsia<ImplClass>::_StartWeaveTimer(uint32_t aMilliseconds)
{
    return WEAVE_NO_ERROR;
}

// Fully instantiate the generic implementation class in whatever compilation unit includes this file.
template class GenericPlatformManagerImpl_Fuchsia<PlatformManagerImpl>;

} // namespace Internal
} // namespace DeviceLayer
} // namespace Weave
} // namespace nl

#endif // GENERIC_PLATFORM_MANAGER_IMPL_FUCHSIA_IPP
