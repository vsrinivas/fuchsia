// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODEC_FACTORY_CODEC_FACTORY_APP_H_
#define GARNET_BIN_MEDIA_CODEC_FACTORY_CODEC_FACTORY_APP_H_

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/app/cpp/startup_context.h>
#include <lib/fsl/tasks/message_loop.h>

#include <list>
#include <memory>

namespace codec_factory {

// CodecFactoryApp is singleton per-process.
class CodecFactoryApp {
 public:
  CodecFactoryApp(std::unique_ptr<fuchsia::sys::StartupContext> startup_context,
                  async::Loop* loop);

  // The caller must only call this on the FIDL thread, and the returned * is
  // only valid for use until the caller returns from the caller's work on the
  // FIDL thread.  The caller must not stash the returned * beyond the caller's
  // return from the caller's work on the FIDL thread, as the next item of work
  // on the FIDL thread could ~CodecFactoryPtr or similar.
  //
  // This method can return nullptr if a HW decoder isn't found.
  const fuchsia::mediacodec::CodecFactoryPtr* FindHwDecoder(
      fit::function<bool(const fuchsia::mediacodec::CodecDescription&)>
          is_match);

 private:
  struct CodecListEntry {
    fuchsia::mediacodec::CodecDescription description;
    // When a HW-accelerated CodecFactory supports more than one sort of codec,
    // the CodecFactory will have multiple entries that share the CodecFactory
    // via the shared_ptr<> here.  The relevant entries co-own the
    // CodecFactoryPtr, and a shared_ptr<> ref is only transiently held by any
    // other code (not posted; not sent across threads).  FWIW, this
    // shared_ptr<> is used in a manner analogous to a Rust RC.
    std::shared_ptr<fuchsia::mediacodec::CodecFactoryPtr> factory;
  };

  void DiscoverMediaCodecDrivers();

  std::unique_ptr<fuchsia::sys::StartupContext> startup_context_;

  async::Loop* loop_;

  // We don't keep a fidl::BindingSet<> here, as we want each CodecFactory
  // instance to delete itself if an error occurs on its channel.
  //
  // The App layer is just here to create CodecFactory instances, each
  // independently bound to its own channel using a std::unique_ptr ImplPtr so
  // that if the channel closes, the CodecFactory instance will go away.  And
  // if the CodecFactory instance wants to self-destruct, it can delete the
  // binding, which will close the channel and delete the CodecFactory.
  // This is true despite comments in the binding code that constantly say how
  // ImplPtr isn't taking ownership; as long as we use std::unique_ptr as
  // ImplPtr it actaully willl take ownership.
  //
  // We create a new instance of CodecFactory for each interface request,
  // because CodecFactory's implementation isn't stateless, by design, for
  // more plausible interface evolution over time.

  // This maps from mime type to hw-based (driver-based) codec factory.  For
  // now, the first driver discovered that supports decoding a given mime type
  // will be chosen to decode that mime type, with an optional fallback to SW if
  // no driver supports the requested mime type.
  //
  // We rely on each create request being self-contained in the CodecFactory
  // interface.
  //
  // For now, items are added at the end of this list as codecs are discovered,
  // removed as channel failure is detected, and when looking for a HW codec the
  // first matching item in the list is selected, if any.
  //
  // This is only read or written from the main FIDL thread.
  std::list<std::unique_ptr<CodecListEntry>> hw_codecs_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CodecFactoryApp);
};

}  // namespace codec_factory

#endif  // GARNET_BIN_MEDIA_CODEC_FACTORY_CODEC_FACTORY_APP_H_
