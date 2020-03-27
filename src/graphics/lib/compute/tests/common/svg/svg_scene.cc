// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tests/common/svg/svg_scene.h"

#include <map>
#include <set>

#include "tests/common/path_sink.h"
#include "tests/common/svg/svg_path_sink.h"
#include "tests/common/svg/svg_utils.h"

#define DEBUG 0

#if DEBUG
#include <stdio.h>
#define LOG(...) fprintf(stderr, __VA_ARGS__)
#else
#define LOG(...) ((void)0)
#endif

namespace {

// Implements a vector of items of type T that cannot contain duplicates.
// Uses an std::map<> to achieve this, using KEY_COMPARE as the comparison
// function.
template <typename T, typename KEY_COMPARE = std::less<T>>
class UniqueVector {
 public:
  UniqueVector() = default;

  // Try to find |key|, and return its index if present. Otherwise, append it
  // to the vector and return its index.
  uint32_t
  findOrCreate(const T & key)
  {
    uint32_t index;
    auto     it = map_.find(key);
    if (it == map_.end())
      {
        items_.push_back(key);
        index = static_cast<uint32_t>(items_.size() - 1u);
        map_.try_emplace(key, index);
      }
    else
      {
        index = it->second;
      }
    return index;
  }

  // Find item |key| in vector and return its index, or UINT32_MAX if not found.
  uint32_t
  find(const T & key) const
  {
    auto it = map_.find(key);
    if (it == map_.end())
      return UINT32_MAX;

    return it->second;
  }

  // Return const reference to the items in the vector.
  const std::vector<T> &
  vector() const
  {
    return items_;
  }

 protected:
  std::vector<T>                     items_;
  std::map<T, uint32_t, KEY_COMPARE> map_;
};

// Helper struct used in SvgScene::Path and SvgScene::Raster containers.
struct PathCompare
{
  bool
  operator()(const SvgScene::Path & a, const SvgScene::Path & b) const noexcept
  {
    if (a.svg_index < b.svg_index)
      return true;
    if (a.svg_index > b.svg_index)
      return false;
    if (a.path_id < b.path_id)
      return true;
    if (a.path_id > b.path_id)
      return false;
    return false;
  }
};

struct RasterCompare
{
  bool
  operator()(const SvgScene::Raster & a, const SvgScene::Raster & b) const noexcept
  {
    if (a.svg_index < b.svg_index)
      return true;
    if (a.svg_index > b.svg_index)
      return false;
    if (a.path_index < b.path_index)
      return true;
    if (a.path_index > b.path_index)
      return false;
    return affine_transform_less(&a.transform, &b.transform);
  }
};

}  // namespace

class SvgScene::Impl {
 public:
  explicit Impl(const std::vector<SvgScene::Item> & items)
  {
    // Maps (item index, raster_id) -> raster_index.
    std::map<std::tuple<uint32_t, uint32_t>, uint32_t> raster_id_to_index;

    // First, decode all paths and rasters into unique sets.
    uint32_t item_index = 0;
    LOG("---- svgscene: decode paths and rasters\n");
    for (const auto & item : items)
      {
        uint32_t svg_index = svgs_.findOrCreate(item.svg);

        svg_decode_rasters(item.svg, &item.transform, [&](const SvgDecodedRaster & r) {
          SvgScene::Path path_key = {
            .svg_index = svg_index,
            .path_id   = r.path_id,
          };

          uint32_t path_index = paths_.findOrCreate(path_key);

          // NOTE: Due to RasterCompare, raster_id will be ignored except when
          // inserting new items in |rasters_|. A way to map that ID to the relevant
          // |rasters_| index later is needed, hence the use of |raster_ids_to_index|.
          SvgScene::Raster raster_key = {
            .svg_index  = svg_index,
            .raster_id  = r.raster_id,
            .path_index = path_index,
            .transform  = r.transform,
          };
          uint32_t raster_index = rasters_.findOrCreate(raster_key);
          raster_id_to_index.try_emplace(std::make_pair(item_index, r.raster_id), raster_index);

          LOG("item_index:%u svg_index:%u r.path_id:%u path_index:%u r.raster_id:%u "
              "raster_index:%u\n",
              item_index,
              svg_index,
              r.path_id,
              path_index,
              r.raster_id,
              raster_index);

          return true;
        });
        item_index++;
      }

    // Second, decode layers.
    {
      LOG("---- svgscene: decode layers\n");
      uint32_t layer_base = 0;
      uint32_t item_index = 0;
      for (const auto & item : items)
        {
          uint32_t svg_index = svgs_.find(item.svg);

          svg_decode_layers(item.svg, [&](const SvgDecodedLayer & l) -> bool {
            LOG("item_index:%u svg_index:%u l.layer_id:%u l.fill_color:%08x l.fill_opacity:%g "
                "l.opacity:%g l.fill_even_odd:%s\n",
                item_index,
                svg_index,
                l.layer_id,
                l.fill_color,
                l.fill_opacity,
                l.opacity,
                l.fill_even_odd ? "true" : "false");

            Layer layer = {
              .svg_index     = svg_index,
              .layer_id      = layer_base + l.layer_id,
              .fill_color    = l.fill_color,
              .fill_opacity  = l.fill_opacity * l.opacity,
              .fill_even_odd = l.fill_even_odd,
            };

            for (const SvgDecodedLayer::Print & print : l.prints)
              {
                // IMPORTANT: print.raster_id might reference a raster that was never
                // decoded, because it corresponds to SVG PathStroke commands that are
                // not implemented, ignore these.
                auto it = raster_id_to_index.find(std::make_pair(item_index, print.raster_id));
                if (it == raster_id_to_index.end())
                  continue;

                uint32_t raster_index = it->second;
                LOG("  raster_id:%u raster_index:%u tx:%d tx:%d\n",
                    print.raster_id,
                    raster_index,
                    print.tx,
                    print.ty);

                layer.prints.push_back(Print{
                  .raster_index = raster_index,
                  .tx           = print.tx,
                  .ty           = print.ty,
                });
              }

            layers_.push_back(std::move(layer));
            return true;
          });

          layer_base += svg_layer_count(item.svg);
          item_index++;
        }
    }
  }

  const std::vector<const svg *> &
  unique_svgs() const
  {
    return svgs_.vector();
  }

  const std::vector<Path> &
  unique_paths() const
  {
    return paths_.vector();
  }

  const std::vector<Raster> &
  unique_rasters() const
  {
    return rasters_.vector();
  }

  const std::vector<Layer> &
  layers() const
  {
    return layers_;
  }

 protected:
  // Unique svgs map.
  UniqueVector<const svg *>                     svgs_;
  UniqueVector<SvgScene::Path, PathCompare>     paths_;
  UniqueVector<SvgScene::Raster, RasterCompare> rasters_;

  // svg_index -> raster_id -> index in rasters_.
  std::map<uint32_t, std::map<uint32_t, uint32_t>> rasters_to_unique_index_;

  std::vector<SvgScene::Layer> layers_;
};

SvgScene::SvgScene() = default;

SvgScene::~SvgScene()
{
  invalidate();
}

void
SvgScene::reset()
{
  invalidate();
  items_.clear();
}

void
SvgScene::addSvgDocument(const struct svg * svg)
{
  addSvgDocument(svg, affine_transform_identity);
}

void
SvgScene::addSvgDocument(const struct svg * svg, double tx, double ty)
{
  addSvgDocument(svg, affine_transform_make_translation(tx, ty));
}

void
SvgScene::addSvgDocument(const struct svg * svg, affine_transform_t transform)
{
  invalidate();
  items_.push_back(Item{ svg, transform });
}

const std::vector<const svg *>
SvgScene::unique_svgs() const
{
  ensureUpdated();
  return impl_->unique_svgs();
}

const std::vector<SvgScene::Path> &
SvgScene::unique_paths() const
{
  ensureUpdated();
  return impl_->unique_paths();
}

const std::vector<SvgScene::Raster> &
SvgScene::unique_rasters() const
{
  ensureUpdated();
  return impl_->unique_rasters();
}

const std::vector<SvgScene::Layer> &
SvgScene::layers() const
{
  ensureUpdated();
  return impl_->layers();
}

void
SvgScene::invalidate()
{
  delete impl_;
  impl_          = nullptr;
  update_needed_ = true;
}

void
SvgScene::update() const
{
  if (update_needed_)
    {
      impl_          = new SvgScene::Impl(items_);
      update_needed_ = false;
    }
}

void
SvgScene::getBounds(double * xmin, double * ymin, double * xmax, double * ymax)
{
  ensureUpdated();

  BoundingPathSink sink;

  const auto & svgs  = impl_->unique_svgs();
  const auto & paths = impl_->unique_paths();

  for (const Raster & r : impl_->unique_rasters())
    svg_decode_path(svgs[r.svg_index], paths[r.path_index].path_id, &r.transform, &sink);

  *xmin = sink.bounds().xmin;
  *ymin = sink.bounds().ymin;
  *xmax = sink.bounds().xmax;
  *ymax = sink.bounds().ymax;
}
