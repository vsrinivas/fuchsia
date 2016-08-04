// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/gl/gles2/texture_cache.h"

#include "ftl/logging.h"

namespace escher {
namespace gles2 {

TextureCache::TextureCache() {}

TextureCache::~TextureCache() {}

Texture TextureCache::GetDepthTexture(const SizeI& size) {
  TextureSpec spec;
  spec.size = size;
  spec.format = TextureSpec::Format::kDepth;
  return GetTexture(spec);
}

Texture TextureCache::GetColorTexture(const SizeI& size) {
  TextureSpec spec;
  spec.size = size;
  spec.format = TextureSpec::Format::kRGBA;
  return GetTexture(spec);
}

Texture TextureCache::GetMipmappedColorTexture(const SizeI& size) {
  TextureSpec spec;
  spec.size = size;
  spec.format = TextureSpec::Format::kRGBA;
  spec.SetFlag(TextureSpec::Flag::kMipmapped);
  return GetTexture(spec);
}

Texture TextureCache::GetTexture(const TextureSpec& spec) {
  // Find the ID corresponding to a suitable existing texture, or create one.
  GLuint id = 0;
  auto it = cache_.find(spec);
  if (it != cache_.end()) {
    id = it->second;
    cache_.erase(it);
    FTL_DCHECK(id != 0);
  } else {
    id = MakeTexture(spec);
  }

  if (!id)
    return Texture(spec, 0);

  ++used_texture_count_;
  // Done this way instead of MakeRefCounted<TextureNeed>(spec, id, this)
  // so that we don't create a RefPtr<TextureNeed> when we want a RefPtr<Need>.
  Need* need = new TextureNeed(spec, id, this);
  return Texture(spec, id, ftl::AdoptRef(need));
}

GLuint TextureCache::MakeTexture(const TextureSpec& spec) const {
  FTL_DCHECK(glGetError() == GL_NO_ERROR);

  // Renderbuffers not supported yet... they're currently created explicitly,
  // not managed by the cache.
  FTL_DCHECK(!spec.HasFlag(TextureSpec::Flag::kRenderbuffer));

  // TODO(jjosh): this isn't exhaustive.
  FTL_DCHECK(spec.format != TextureSpec::Format::kDepth ||
             !spec.HasFlag(TextureSpec::Flag::kMipmapped));

  GLuint id = 0;
  glGenTextures(1, &id);
  if (!id) {
    FTL_DLOG(WARNING) << "failed to generate texture.";
    return 0;
  }

  glBindTexture(GL_TEXTURE_2D, id);
  if (spec.HasFlag(TextureSpec::Flag::kMipmapped)) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 spec.size.width(), spec.size.height(),
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glGenerateMipmap(GL_TEXTURE_2D);
  } else {
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GLenum format;
    GLenum type;
    switch (spec.format) {
      case TextureSpec::Format::kRGBA:
        format = GL_RGBA;
        type = GL_UNSIGNED_BYTE;
        break;
      case TextureSpec::Format::kDepth:
        format = GL_DEPTH_COMPONENT;
        type = GL_UNSIGNED_SHORT;
        break;
      default:
        glDeleteTextures(1, &id);
        return 0;
    }
    glTexImage2D(GL_TEXTURE_2D, 0, format,
                 spec.size.width(), spec.size.height(),
                 0, format, type, nullptr);
  }

  FTL_DCHECK(glGetError() == GL_NO_ERROR);
  return id;
}

void TextureCache::DeleteTextures(size_t count, GLuint* ids) const {
  glDeleteTextures(static_cast<GLuint>(count), ids);
  FTL_DCHECK(glGetError() == GL_NO_ERROR);
}

void TextureCache::Clear() {
  std::vector<GLuint> ids;
  ids.reserve(cache_.size());
  for (auto& pair : cache_)
    ids.push_back(pair.second);
  DeleteTextures(ids.size(), &ids[0]);

  cache_.clear();
}

void TextureCache::RecieveTexture(TextureSpec spec, GLuint id) {
  cache_.emplace(spec, id);
  --used_texture_count_;
}

TextureCache::TextureNeed::TextureNeed(
    const TextureSpec& spec, GLuint id, TextureCache* cache)
    : spec_(spec), id_(id), cache_(cache) {}

TextureCache::TextureNeed::~TextureNeed() {
  cache_->RecieveTexture(std::move(spec_), id_);
}

}  // namespace gles2
}  // namespace escher
