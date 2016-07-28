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
  TextureDescriptor descriptor;
  descriptor.size = size;
  descriptor.format = TextureDescriptor::Format::kDepth;
  return GetTexture(descriptor);
}

Texture TextureCache::GetColorTexture(const SizeI& size) {
  TextureDescriptor descriptor;
  descriptor.size = size;
  descriptor.format = TextureDescriptor::Format::kRGBA;
  return GetTexture(descriptor);
}

Texture TextureCache::GetMipmappedColorTexture(const SizeI& size) {
  TextureDescriptor descriptor;
  descriptor.size = size;
  descriptor.format = TextureDescriptor::Format::kRGBA;
  descriptor.mipmapped = true;
  return GetTexture(descriptor);
}

Texture TextureCache::GetTexture(const TextureDescriptor& descriptor) {
  // Find the ID corresponding to a suitable existing texture, or create one.
  GLuint id = 0;
  auto it = cache_.find(descriptor);
  if (it != cache_.end()) {
    id = it->second;
    cache_.erase(it);
    FTL_DCHECK(id != 0);
  } else {
    id = MakeTexture(descriptor);
  }

  if (!id)
    return Texture(descriptor, 0);

  ++used_texture_count_;
  // Done this way instead of MakeRefCounted<TextureNeed>(descriptor, id, this)
  // so that we don't create a RefPtr<TextureNeed> when we want a RefPtr<Need>.
  Need* need = new TextureNeed(descriptor, id, this);
  return Texture(descriptor, id, ftl::AdoptRef(need));
}

GLuint TextureCache::MakeTexture(const TextureDescriptor& desc) const {
  FTL_DCHECK(glGetError() == GL_NO_ERROR);

  // TODO(jjosh): this isn't exhaustive.
  FTL_DCHECK(desc.format != TextureDescriptor::Format::kDepth || !desc.mipmapped);

  GLuint id = 0;
  glGenTextures(1, &id);
  if (!id) {
    FTL_DLOG(WARNING) << "failed to generate texture.";
    return 0;
  }

  glBindTexture(GL_TEXTURE_2D, id);
  if (desc.mipmapped) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 desc.size.width(), desc.size.height(),
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
    switch (desc.format) {
      case TextureDescriptor::Format::kRGBA:
        format = GL_RGBA;
        type = GL_UNSIGNED_BYTE;
        break;
      case TextureDescriptor::Format::kDepth:
        format = GL_DEPTH_COMPONENT;
        type = GL_UNSIGNED_SHORT;
        break;
      default:
        glDeleteTextures(1, &id);
        return 0;
    }
    glTexImage2D(GL_TEXTURE_2D, 0, format,
                 desc.size.width(), desc.size.height(),
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

void TextureCache::RecieveTexture(TextureDescriptor descriptor, GLuint id) {
  cache_.emplace(descriptor, id);
  --used_texture_count_;
}

TextureCache::TextureNeed::TextureNeed(
    const TextureDescriptor& descriptor, GLuint id, TextureCache* cache)
    : descriptor_(descriptor), id_(id), cache_(cache) {}

TextureCache::TextureNeed::~TextureNeed() {
  cache_->RecieveTexture(std::move(descriptor_), id_);
}

}  // namespace gles2
}  // namespace escher
