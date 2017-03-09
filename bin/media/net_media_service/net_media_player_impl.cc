// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/net_media_service/net_media_player_impl.h"

#include "apps/media/services/seeking_reader.fidl.h"
#include "lib/ftl/logging.h"
#include "lib/url/gurl.h"

namespace media {

// static
std::shared_ptr<NetMediaPlayerImpl> NetMediaPlayerImpl::Create(
    const fidl::String& service_name,
    fidl::InterfaceHandle<MediaPlayer> media_player,
    fidl::InterfaceRequest<NetMediaPlayer> net_media_player_request,
    NetMediaServiceImpl* owner) {
  return std::shared_ptr<NetMediaPlayerImpl>(
      new NetMediaPlayerImpl(service_name, std::move(media_player),
                             std::move(net_media_player_request), owner));
}

NetMediaPlayerImpl::NetMediaPlayerImpl(
    const fidl::String& service_name,
    fidl::InterfaceHandle<MediaPlayer> media_player,
    fidl::InterfaceRequest<NetMediaPlayer> net_media_player_request,
    NetMediaServiceImpl* owner)
    : NetMediaServiceImpl::Product<NetMediaPlayer>(
          this,
          std::move(net_media_player_request),
          owner),
      media_player_(MediaPlayerPtr::Create(std::move(media_player))),
      responder_(this, service_name, owner->application_context()) {
  FTL_DCHECK(owner);
  media_service_ = owner->ConnectToEnvironmentService<MediaService>();
}

NetMediaPlayerImpl::~NetMediaPlayerImpl() {}

void NetMediaPlayerImpl::SetUrl(const fidl::String& url_as_string) {
  url::GURL url = url::GURL(url_as_string);

  if (!url.is_valid()) {
    FTL_DLOG(ERROR) << "Invalid URL " << url_as_string << " specified";
    return;
  }

  SeekingReaderPtr reader;

  if (url.SchemeIsFile()) {
    media_service_->CreateFileReader(url.path(), reader.NewRequest());
  } else {
    media_service_->CreateNetworkReader(url_as_string, reader.NewRequest());
  }

  media_player_->SetReader(std::move(reader));
}

void NetMediaPlayerImpl::Play() {
  media_player_->Play();
}

void NetMediaPlayerImpl::Pause() {
  media_player_->Pause();
}

void NetMediaPlayerImpl::Seek(int64_t position) {
  media_player_->Seek(position);
}

void NetMediaPlayerImpl::GetStatus(uint64_t version_last_seen,
                                   const GetStatusCallback& callback) {
  media_player_->GetStatus(version_last_seen, callback);
}

}  // namespace media
