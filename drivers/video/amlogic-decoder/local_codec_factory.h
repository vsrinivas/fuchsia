// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_LOCAL_CODEC_FACTORY_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_LOCAL_CODEC_FACTORY_H_

#include <fuchsia/mediacodec/cpp/fidl.h>

#include <lib/fidl/cpp/binding.h>
#include <lib/fxl/macros.h>

// TODO(dustingreen): Concider pulling LocalCodecFactory out into a source_set
// that can be used by other HW codec drivers (in contrast to CodecImpl source
// set which will need to remain usable for both SW and HW codecs).

class DeviceCtx;

// A LocalCodecFactory is owned by DeviceFidl via a unique_ptr<>.  The channel
// closing can also mandate that DeviceFidl stop owning the LocalCodecFactory.
//
// Unlike a SW codec isolate's local CodecFactory, this HW codec's local
// CodecFactory doesn't self-close after creating one codec.
class LocalCodecFactory : public fuchsia::mediacodec::CodecFactory {
 public:
  // The constructor can run on the IOCTL thread, under the control of the
  // creating/owning DeviceCtx.
  //
  // device - parent device.
  explicit LocalCodecFactory(DeviceCtx* device);

  // If Bind() was previously called, this must run on shared_fidl_thread(). If
  // Bind() has not been called, this can run on IOCTL thread or
  // shared_fidl_thread().
  ~LocalCodecFactory();

  // This needs to be called before Bind(), not after.  The caller's
  // error_handler is called up to once, when/if the channel has an error.
  //
  // If ~LocalCodecFactory() runs before the channel has any error, then
  // error_handler won't be run.
  //
  // This method can be called on the IOCTL thread.
  //
  // The error_handler can be called on the IOCTL thread or the
  // shared_fidl_thread(), but never while SetErrorHandler() is still on the
  // stack.
  void SetErrorHandler(fit::closure error_handler);

  // Until this is called, the LocalCodecFactory won't do anything itself.
  // During/after this call and until destruction, the LocalCodecFactory can
  // create CodecImpl instances on client request(s) using shared_fidl_thread(),
  // and can call error_handler on shared_fidl_thread().
  //
  // Dispatching of CodecFactory interface methods will occur on
  // device_->driver()->shared_fidl_thread().
  //
  // This call can be called on the IOCTL thread.
  //
  // In contrast to Bind(),
  //
  // TODO(dustingreen): Ensure that fidl::Binding::Bind() will explicitly
  // support getting called from a thread that isn't a worker thread of the
  // async_t being bound to. fidl::Binding::Bind() looks able to do this
  // currently, but the comments don't say.
  void Bind(zx::channel server_endpoint);

  //
  // CodecFactory interface
  //

  void CreateDecoder(
      fuchsia::mediacodec::CreateDecoder_Params video_decoder_params,
      ::fidl::InterfaceRequest<fuchsia::mediacodec::Codec> video_decoder)
      override;

  // TODO(dustingreen): Implement just-close-the-request stubs for:
  // audio encoder
  // video encoder
  // (or combine more)

 private:
  DeviceCtx* device_ = nullptr;

  // This binding doesn't channel-own this LocalCodecFactory.  The DeviceFidl
  // owns all the LocalCodecFactory(s).  The DeviceFidl will SetErrorHandler()
  // such that its ownership drops if the channel fails.
  fidl::Binding<fuchsia::mediacodec::CodecFactory, LocalCodecFactory*>
      factory_binding_;

  bool is_error_handler_set_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(LocalCodecFactory);
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_LOCAL_CODEC_FACTORY_H_
