// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_SW_OMX_CODEC_RUNNER_SW_OMX_CODEC_RUNNER_H_
#define GARNET_BIN_MEDIA_CODECS_SW_OMX_CODEC_RUNNER_SW_OMX_CODEC_RUNNER_H_

#include <threads.h>

#include <fuchsia/mediacodec/cpp/fidl.h>

#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/synchronization/thread_annotations.h"

namespace codec_runner {

// This is an abstract base class whose main purpose is to prevent us from
// assuming that all codecs run locally will be OMX codecs.
class CodecRunner : public fuchsia::mediacodec::Codec {
 public:
  // needs a virtual destructor because unique_ptr will be deleting via vtable
  // entry instead of direct call to destructor
  virtual ~CodecRunner();

  // Load() will be called after the derived class constructor.
  virtual bool Load() = 0;

  // Only one of the following SetXXXParams() is called, corresponding to which
  // codec type was requested via CodecFactory.  These are meant to be an easy
  // way to convey the most recent known version of complete codec creation
  // parameters to the CodecRunner.  As such they are not intended to be a
  // complete CodecFactory implementation, nor does this class implement
  // CodecFactory.
  virtual void SetDecoderParams(
      fuchsia::mediacodec::CreateDecoder_Params decoder_params) = 0;
  // TODO(dustingreen):
  // virtual void SetAudioEncoderParams(...) = 0;
  // virtual void SetVideoEncoderParams(...) = 0;
  // (or combined)

  // Now that type-specific params are set, input_constraints_ can be computed.
  // We want this done before binding the Codec channel so we can immediately
  // send the input constraints as soon as BindAndOwnSelf(), to ensure that
  // input constraints get sent first from server to client, per the Codec
  // protocol.
  virtual void ComputeInputConstraints() = 0;

  // This call causes ownership of "this" to transfer to binding_, which
  // essentially makes "this" self-owned (roughly speaking), or slightly more
  // precisely, owned by the Codec channel via ImplPtr of the binding being
  // std::unique_ptr<CodecRunner> here (instead of the default Codec*).
  void BindAndOwnSelf(
      fidl::InterfaceRequest<fuchsia::mediacodec::Codec> codec_request,
      std::unique_ptr<CodecRunner> self);

  // Some sub-classes want to send initial output constraints very early,
  // instead of waiting for any input data.  This can be because the codec
  // implementation isn't capable of waiting until input data has arrived before
  // demanding output buffers despite a tendency (but not guarantee) of forcing
  // re-configuration of those initial output buffers (I'm looking at you OMX),
  // or because the codec really does already know the output buffer constraints
  // based on codec creation info, so doesn't need any input data before
  // indicating output constraints.
  //
  // This intentionally gets called _before_ sending input constraints, so extra
  // output re-config is avoided if the client processes this before sending
  // input data.
  //
  // The default implementation does nothing.
  virtual void onInputConstraintsReady(){};

  // The Setup ordering domain is done.  This allows the items in the Setup
  // ordering domain to be completely separate from the StreamControl ordering
  // domain.
  virtual void onSetupDone(){};

  void Exit(const char* format, ...);

 protected:
  CodecRunner(async_t* fidl_async, thrd_t fidl_thread);

  // Lock that protects stuff.
  //
  // TODO(dustingreen): Figure out which locks and condition variables / events
  // to use from FXL, or how to get FXL_GUARDED_BY() to understand std::mutex +
  // std::unique_lock<> (including when a unique_lock& is passed into a method
  // along the way but will definitely be locked again by the time that method
  // returns) if that's possible.  We should use FXL_GUARDED_BY() where its
  // expressiveness is sufficient.
  std::mutex lock_;

  async_t* const fidl_async_;
  const thrd_t fidl_thread_;
  using BindingType =
      fidl::Binding<fuchsia::mediacodec::Codec, std::unique_ptr<CodecRunner>>;
  std::unique_ptr<BindingType> binding_;

  bool input_constraints_sent_ = false;

  // This must be set by derived class no later than the end of
  // SetAudioDecoderParams() or analogous method, so that this will be
  // guaranteed to be set before Codec binding occurs, so we can send these
  // constraints during BindAndOwnSelf().
  //
  // This remains valid after CodecRunner sends OnInputConstraints(), in case
  // a derived class wants to refer to the input constraints.
  std::unique_ptr<const fuchsia::mediacodec::CodecBufferConstraints>
      input_constraints_;

  FXL_DISALLOW_IMPLICIT_CONSTRUCTORS(CodecRunner);
};

}  // namespace codec_runner

#endif  // GARNET_BIN_MEDIA_CODECS_SW_OMX_CODEC_RUNNER_SW_OMX_CODEC_RUNNER_H_
