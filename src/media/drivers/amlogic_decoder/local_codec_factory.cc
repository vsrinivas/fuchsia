// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "local_codec_factory.h"

#include <lib/fidl/cpp/clone.h>
#include <lib/fit/function.h>
#include <lib/media/codec_impl/codec_admission_control.h>

#include <optional>

#include "codec_adapter_h264.h"
#include "codec_adapter_h264_multi.h"
#include "codec_adapter_mpeg2.h"
#include "codec_adapter_vp9.h"
#include "device_ctx.h"

namespace {

struct CodecAdapterFactory {
  bool is_enabled;
  fuchsia::mediacodec::CodecDescription description;

  // This typedef is just for local readability here, not for use outside this
  // struct.
  using CreateFunction = fit::function<std::unique_ptr<CodecAdapter>(
      std::mutex& lock, CodecAdapterEvents*, DeviceCtx*)>;

  bool multi_instance;

  CreateFunction create;
};

// TODO(dustingreen): Fix up this list to correspond to what
// CodecImpl+AmlogicVideo can actually handle so far, once there's at least one
// format in that list.  For now this list is here to allow covering some
// LocalCodecFactory code.
const CodecAdapterFactory kCodecFactories[] = {
    {
        true,  // is_enabled
        fuchsia::mediacodec::CodecDescription{
            .codec_type = fuchsia::mediacodec::CodecType::DECODER,
            // TODO(dustingreen): See TODO comments on this field in
            // codec_common.fidl.
            .mime_type = "video/h264",

            // TODO(fxbug.dev/13483): Make more of these true by implementing in
            // CodecAdapterH264Multi.
            .can_stream_bytes_input = true,
            .can_find_start = false,
            .can_re_sync = false,
            .will_report_all_detected_errors = false,

            .is_hw = true,

            // TODO(dustingreen): Determine if this claim of "true" is actually
            // the case.
            .split_header_handling = true,
        },
        true,  // multi_instance
        [](std::mutex& lock, CodecAdapterEvents* events, DeviceCtx* device) {
          return std::make_unique<CodecAdapterH264Multi>(lock, events, device);
        },
    },
    {
        false,  // is_enabled
        fuchsia::mediacodec::CodecDescription{
            .codec_type = fuchsia::mediacodec::CodecType::DECODER,
            // TODO(dustingreen): See TODO comments on this field in
            // codec_common.fidl.
            .mime_type = "video/mpeg2",

            // TODO(dustingreen): Determine which of these can safely indicate
            // more capability.
            .can_stream_bytes_input = false,
            .can_find_start = false,
            .can_re_sync = false,
            .will_report_all_detected_errors = false,

            .is_hw = true,

            // TODO(dustingreen): Determine if this claim of "true" is actually
            // the case.
            .split_header_handling = true,
        },
        false,  // multi_instance
        [](std::mutex& lock, CodecAdapterEvents* events, DeviceCtx* device) {
          return std::make_unique<CodecAdapterMpeg2>(lock, events, device);
        },
    },
    {
        true,  // is_enabled
        fuchsia::mediacodec::CodecDescription{
            .codec_type = fuchsia::mediacodec::CodecType::DECODER,
            // TODO(dustingreen): See TODO comments on this field in
            // codec_common.fidl.
            .mime_type = "video/vp9",

            // TODO(dustingreen): Determine which of these can safely indicate
            // more capability.
            .can_stream_bytes_input = false,
            .can_find_start = false,
            .can_re_sync = false,
            .will_report_all_detected_errors = false,

            .is_hw = true,

            // TODO(dustingreen): Determine if this claim of "true" is actually
            // the case.
            .split_header_handling = true,
        },
        true,  // multi_instance
        [](std::mutex& lock, CodecAdapterEvents* events, DeviceCtx* device) {
          return std::make_unique<CodecAdapterVp9>(lock, events, device);
        },
    },
    {
        true,  // is_enabled
        fuchsia::mediacodec::CodecDescription{
            .codec_type = fuchsia::mediacodec::CodecType::DECODER,
            // TODO(dustingreen): See TODO comments on this field in
            // codec_common.fidl.
            .mime_type = "video/h264-single",

            // TODO(dustingreen): Determine which of these can safely indicate
            // more capability.
            .can_stream_bytes_input = false,
            .can_find_start = false,
            .can_re_sync = false,
            .will_report_all_detected_errors = false,

            .is_hw = true,

            // TODO(dustingreen): Determine if this claim of "true" is actually
            // the case.
            .split_header_handling = true,
        },
        false,  // multi_instance
        [](std::mutex& lock, CodecAdapterEvents* events, DeviceCtx* device) {
          return std::make_unique<CodecAdapterH264>(lock, events, device);
        },
    },
    {
        true,  // is_enabled
        fuchsia::mediacodec::CodecDescription{
            .codec_type = fuchsia::mediacodec::CodecType::DECODER,
            // TODO(dustingreen): See TODO comments on this field in
            // codec_common.fidl.
            .mime_type = "video/h264-multi",

            // TODO(dustingreen): Determine which of these can safely indicate
            // more capability.
            .can_stream_bytes_input = false,
            .can_find_start = false,
            .can_re_sync = false,
            .will_report_all_detected_errors = false,

            .is_hw = true,

            // TODO(dustingreen): Determine if this claim of "true" is actually
            // the case.
            .split_header_handling = true,
        },
        true,  // multi_instance
        [](std::mutex& lock, CodecAdapterEvents* events, DeviceCtx* device) {
          return std::make_unique<CodecAdapterH264Multi>(lock, events, device);
        },
    },
    {
        true,  // is_enabled
        fuchsia::mediacodec::CodecDescription{
            .codec_type = fuchsia::mediacodec::CodecType::DECODER,
            // TODO(dustingreen): See TODO comments on this field in
            // codec_common.fidl.
            .mime_type = "video/h264-multi/test/force-context-save-restore",

            // TODO(dustingreen): Determine which of these can safely indicate
            // more capability.
            .can_stream_bytes_input = false,
            .can_find_start = false,
            .can_re_sync = false,
            .will_report_all_detected_errors = false,

            .is_hw = true,

            // TODO(dustingreen): Determine if this claim of "true" is actually
            // the case.
            .split_header_handling = true,
        },
        true,  // multi_instance
        [](std::mutex& lock, CodecAdapterEvents* events, DeviceCtx* device) {
          auto decoder = std::make_unique<CodecAdapterH264Multi>(lock, events, device);
          AmlogicDecoderTestHooks test_hooks = {
              .force_context_save_restore = true,
          };
          decoder->set_test_hooks(test_hooks);
          return decoder;
        },
    },
};

}  // namespace

// device - associated device.
LocalCodecFactory::LocalCodecFactory(DeviceCtx* device) : device_(device), factory_binding_(this) {
  // nothing else to do here
}

LocalCodecFactory::~LocalCodecFactory() {
  // We need ~factory_binding_ to run on shared_fidl_thread() else it's not safe
  // to un-bind unilaterally (without the channel closing).  Unless not bound in
  // the first place.
  ZX_DEBUG_ASSERT(thrd_current() == device_->driver()->shared_fidl_thread() ||
                  !factory_binding_.is_bound());

  // ~factory_binding_ here + fact that we're running on shared_fidl_thread()
  // (if Bind() previously called) means error_handler won't be running
  // concurrently with ~LocalCodecFactory and won't run after ~factory_binding_
  // here.
}

void LocalCodecFactory::SetErrorHandler(fit::closure error_handler) {
  ZX_DEBUG_ASSERT(!factory_binding_.is_bound());
  factory_binding_.set_error_handler(
      [this, error_handler = std::move(error_handler)](zx_status_t status) mutable {
        ZX_DEBUG_ASSERT(thrd_current() == device_->driver()->shared_fidl_thread());
        // This queues after the similar posting in CreateDecoder() (via
        // TryAddCodec()), so that LocalCodecFactory won't get deleted until
        // after previously-started TryAddCodec()s are done.
        device_->codec_admission_control()->PostAfterPreviouslyStartedClosesDone(
            [this, error_handler = std::move(error_handler)] {
              ZX_DEBUG_ASSERT(thrd_current() == device_->driver()->shared_fidl_thread());
              error_handler();
              // "this" is gone
            });
      });
  is_error_handler_set_ = true;
}

void LocalCodecFactory::Bind(zx::channel server_endpoint) {
  ZX_DEBUG_ASSERT(is_error_handler_set_);
  ZX_DEBUG_ASSERT(!factory_binding_.is_bound());

  // Go!  (immediately - if Bind() is called on IOCTL thread, this can result in
  // _immediate_ dispatching over on shared_fidl_thread()).
  factory_binding_.Bind(std::move(server_endpoint),
                        device_->driver()->shared_fidl_loop()->dispatcher());

  // All HW-accelerated local CodecFactory(s) must send OnCodecList()
  // immediately upon creation of the local CodecFactory.
  std::vector<fuchsia::mediacodec::CodecDescription> codec_descriptions;
  for (const CodecAdapterFactory& factory : kCodecFactories) {
    if (!factory.is_enabled) {
      continue;
    }
    codec_descriptions.push_back(fidl::Clone(factory.description));
  }
  factory_binding_.events().OnCodecList(std::move(codec_descriptions));
}

void LocalCodecFactory::CreateDecoder(
    fuchsia::mediacodec::CreateDecoder_Params video_decoder_params,
    ::fidl::InterfaceRequest<fuchsia::media::StreamProcessor> video_decoder) {
  if (!video_decoder_params.has_input_details()) {
    return;
  }

  if (!video_decoder_params.input_details().has_mime_type()) {
    // Without mime_type we cannot search for a decoder.
    return;
  }

  const CodecAdapterFactory* factory = nullptr;
  for (const CodecAdapterFactory& candidate_factory : kCodecFactories) {
    if (!candidate_factory.is_enabled) {
      continue;
    }
    if (candidate_factory.description.mime_type ==
        video_decoder_params.input_details().mime_type()) {
      factory = &candidate_factory;
      break;
    }
  }
  if (!factory) {
    // This shouldn't really happen since the main CodecFactory shouldn't be
    // asking this LocalCodecFactory for a codec fitting a description that's
    // not a description this factory previously delivered to the main
    // CodecFactory via OnCodecList().
    //
    // TODO(dustingreen): epitaph for video_decoder.
    //
    // ~video_decoder here will take care of closing the channel
    return;
  }

  // We also post to the same queue in the set_error_handler() lambda, so that
  // we know the LocalCodecFactory will remain alive until after this lambda
  // completes.
  //
  // The factory pointer remains valid for whole lifetime of this devhost
  // process.
  device_->codec_admission_control()->TryAddCodec(
      factory->multi_instance, [this, video_decoder_params = std::move(video_decoder_params),
                                video_decoder = std::move(video_decoder),
                                factory](std::unique_ptr<CodecAdmission> codec_admission) mutable {
        if (!codec_admission) {
          // We can't create another Codec presently.
          //
          // ~video_decoder will take care of closing the channel.
          return;
        }

        fidl::InterfaceHandle<fuchsia::sysmem::Allocator> sysmem =
            device_->video()->ConnectToSysmem();
        if (!sysmem) {
          return;
        }

        std::unique_ptr<CodecImpl> codec =
            std::make_unique<CodecImpl>(std::move(sysmem), std::move(codec_admission),
                                        device_->driver()->shared_fidl_loop()->dispatcher(),
                                        device_->driver()->shared_fidl_thread(),
                                        std::move(video_decoder_params), std::move(video_decoder));

        codec->SetCoreCodecAdapter(factory->create(codec->lock(), codec.get(), device_));

        device_->device_fidl()->BindCodecImpl(std::move(codec));
      });
}

void LocalCodecFactory::CreateEncoder(
    fuchsia::mediacodec::CreateEncoder_Params encoder_params,
    ::fidl::InterfaceRequest<fuchsia::media::StreamProcessor> encoder_request) {
  // We have no encoders to provide.
  // ~encoder_request
}
