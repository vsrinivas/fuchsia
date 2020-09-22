// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/scenic/internal/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace-provider/provider.h>
#include <png.h>

#include <cstdlib>
#include <iostream>
#include <memory>

#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include "src/lib/fsl/vmo/vector.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/ui/scenic/lib/gfx/snapshot/snapshot_generated.h"
#include "src/ui/scenic/lib/gfx/snapshot/version.h"
#include "third_party/modp_b64/modp_b64.h"

using namespace rapidjson;
using namespace scenic_impl::gfx;

const char *EMPTY_GLTF_DOC = R"glTF({
  "scenes": [{
    "nodes": []
  }],
  "scene": 0,
  "nodes": [],
  "meshes": [],
  "buffers": [],
  "bufferViews": [],
  "accessors": [],
  "materials": [],
  "textures": [],
  "images": [],
  "samplers": [{}],
  "asset": {
    "version": "2.0"
  }
})glTF";

const char *EMPTY_TEXTURE_MATERIAL = R"glTF({
   "pbrMetallicRoughness" : {
    "baseColorTexture" : {
    },
    "metallicFactor" : 0.0,
    "roughnessFactor" : 1.0
  }
})glTF";

const char *EMPTY_COLOR_MATERIAL = R"glTF({
   "pbrMetallicRoughness" : {
    "baseColorFactor" : [1.0, 1.0, 1.0, 1.0],
    "metallicFactor" : 0.0,
    "roughnessFactor" : 1.0
  }
})glTF";

// Converts uncompressed raw image to PNG.
bool RawToPNG(size_t width, size_t height, const uint8_t *data, std::vector<uint8_t> &out);

// Encode bytes to base64.
std::string Base64Encode(const uint8_t *bytes, size_t size);

// Dumps rapidjson Value to ostream.
std::ostream &operator<<(std::ostream &os, const Value &v) {
  StringBuffer buffer;
  PrettyWriter<StringBuffer> writer(buffer);
  v.Accept(writer);
  return os << buffer.GetString();
}

// Defines a class to take a snapshot of the current scenic composition.
class SnapshotTaker {
 public:
  explicit SnapshotTaker(async::Loop *loop)
      : loop_(loop), context_(sys::ComponentContext::CreateAndServeOutgoingDirectory()) {
    // Connect to the Scenic service.
    scenic_ = context_->svc()->Connect<fuchsia::ui::scenic::Scenic>();
    scenic_.set_error_handler([this](zx_status_t status) {
      FX_LOGS(ERROR) << "Lost connection to Scenic service.";
      encountered_error_ = true;
      loop_->Quit();
    });

    // Connect to the internal snapshot service.
    snapshotter_ = context_->svc()->Connect<fuchsia::ui::scenic::internal::Snapshot>();
    snapshotter_.set_error_handler([this](zx_status_t status) {
      FX_LOGS(ERROR) << "Lost connection to Snapshot service.";
      encountered_error_ = true;
      loop_->Quit();
    });
  }

  bool encountered_error() const { return encountered_error_; }

  // Takes a snapshot of the current scenic composition and dumps it to
  // std::out in glTF format.
  void TakeSnapshot() {
    // If we wait for a call back from GetDisplayInfo, we are guaranteed that
    // the GFX system is initialized, which is a prerequisite for taking a
    // screenshot. TODO(fxbug.dev/23901): Remove call to GetDisplayInfo once bug done.
    scenic_->GetDisplayInfo([this](fuchsia::ui::gfx::DisplayInfo /*unused*/) {
      snapshotter_->TakeSnapshot(
          [this](std::vector<fuchsia::ui::scenic::internal::SnapshotResult> results) {
            if (results.size() == 0) {
              FX_LOGS(INFO) << "No compositors found.";
              loop_->Quit();
            }

            // Although multiple results can be returned, one for each compositor, the glTF exporter
            // currently only makes use of the first compositor that is found.
            if (results.size() > 1) {
              FX_LOGS(WARNING) << "Multiple snapshot buffers were returned, but glTF exporter is "
                                  "only using the first one.";
            }

            const auto &result = results[0];
            if (!result.success) {
              FX_LOGS(ERROR) << "Snapshot was not successful.";
              encountered_error_ = true;
              loop_->Quit();
            }

            const auto &buffer = result.buffer;
            std::vector<uint8_t> data;
            if (!fsl::VectorFromVmo(buffer, &data)) {
              FX_LOGS(ERROR) << "TakeSnapshot failed";
              encountered_error_ = true;
              loop_->Quit();
              return;
            }

            // We currently support flatbuffer.v1_0 format.
            auto snapshot = (const SnapshotData *)data.data();
            if (snapshot->type != SnapshotData::SnapshotType::kFlatBuffer ||
                snapshot->version != SnapshotData::SnapshotVersion::v1_0) {
              FX_LOGS(ERROR) << "Invalid snapshot format encountered. Aborting.";
              encountered_error_ = true;
              loop_->Quit();
              return;
            }

            // De-serialize the snapshot from flatbuffer.
            auto node = flatbuffers::GetRoot<snapshot::Node>(snapshot->data);

            // Start with an empty glTF document.
            document_.Parse(EMPTY_GLTF_DOC);

            // Export root node of the scene graph. This recursively exports all
            // descendant nodes.
            int index = glTF_export_node(node, true);
            auto &gltf_nodes = document_["scenes"][0]["nodes"];
            gltf_nodes.PushBack(index, document_.GetAllocator());

            // Dump the result json document in glTF format to stdout.
            std::cout << document_;

            loop_->Quit();
          });
    });
  }

 private:
  int glTF_export_node(const snapshot::Node *node, bool flip_yaxis = false) {
    // The allocator used through out.
    auto &allocator = document_.GetAllocator();

    auto gltf_node = Value(kObjectType);
    if (node->name()) {
      gltf_node.AddMember("name", node->name()->str(), allocator);
    }

    if (node->transform()) {
      auto translation = node->transform()->translation();
      if (translation->x() != 0.0 || translation->y() != 0.0 || translation->z() != 0.0) {
        auto gltf_translation = Value(kArrayType);
        gltf_translation.PushBack(translation->x(), allocator);
        gltf_translation.PushBack(translation->y(), allocator);
        gltf_translation.PushBack(-translation->z(), allocator);
        gltf_node.AddMember("translation", gltf_translation, allocator);
      }

      auto rotation = node->transform()->rotation();
      if (rotation->x() != 0.0 || rotation->y() != 0.0 || rotation->z() != 0.0 ||
          rotation->w() != 1.0) {
        auto gltf_rotation = Value(kArrayType);
        gltf_rotation.PushBack(rotation->x(), allocator);
        gltf_rotation.PushBack(rotation->y(), allocator);
        gltf_rotation.PushBack(rotation->z(), allocator);
        gltf_rotation.PushBack(rotation->w(), allocator);
        gltf_node.AddMember("rotation", gltf_rotation, allocator);
      }

      auto scale = node->transform()->scale();
      if (scale->x() != 1.0 || scale->y() != 1.0 || scale->z() != 1.0) {
        auto gltf_scale = Value(kArrayType);
        gltf_scale.PushBack(scale->x(), allocator);
        if (flip_yaxis) {
          gltf_scale.PushBack(scale->y() * -1, allocator);
        } else {
          gltf_scale.PushBack(scale->y(), allocator);
        }
        gltf_scale.PushBack(scale->z(), allocator);
        gltf_node.AddMember("scale", gltf_scale, allocator);
      }
    }

    if (node->mesh()) {
      int index = glTF_export_mesh(node);
      gltf_node.AddMember("mesh", index, allocator);
    }

    auto &gltf_nodes = document_["nodes"];
    auto index = gltf_nodes.Size();
    gltf_nodes.PushBack(gltf_node, allocator);

    if (node->children()) {
      auto child_nodes = Value(kArrayType);
      for (auto child : *node->children()) {
        int index = glTF_export_node(child);
        child_nodes.PushBack(index, allocator);
      }
      auto &gltf_node = document_["nodes"][index];
      gltf_node.AddMember("children", child_nodes, allocator);
    }
    return index;
  }

  int glTF_export_mesh(const snapshot::Node *node) {
    // The allocator used through out.
    auto &allocator = document_.GetAllocator();

    auto gltf_primitive = Value(kObjectType);
    gltf_primitive.AddMember("material", glTF_export_material(node), allocator);
    gltf_primitive.AddMember("attributes", glTF_export_buffer(node->mesh(), true), allocator);
    gltf_primitive.AddMember("indices", glTF_export_buffer(node->mesh(), false), allocator);

    auto gltf_primitives = Value(kArrayType);
    gltf_primitives.PushBack(gltf_primitive, allocator);

    auto gltf_mesh = Value(kObjectType);
    gltf_mesh.AddMember("primitives", gltf_primitives, allocator);

    auto &gltf_meshes = document_["meshes"];
    auto index = gltf_meshes.Size();
    gltf_meshes.PushBack(gltf_mesh, allocator);

    return index;
  }

  Value glTF_export_buffer(const snapshot::Geometry *mesh, bool is_vertex_buffer) {
    auto &allocator = document_.GetAllocator();

    const uint8_t *bytes = is_vertex_buffer ? mesh->attributes()->Get(0)->buffer()->Data()
                                            : mesh->indices()->buffer()->Data();
    size_t size = is_vertex_buffer ? mesh->attributes()->Get(0)->buffer()->Length()
                                   : mesh->indices()->buffer()->Length();
    int count = is_vertex_buffer ? mesh->attributes()->Get(0)->vertex_count()
                                 : mesh->indices()->index_count();

    // Create a glTF buffer.
    std::string base64_bytes = Base64Encode(bytes, size);
    std::ostringstream data;
    data << "data:application/octet-stream;base64," << base64_bytes;

    auto data_str = data.str();
    Value data_uri(kStringType);
    data_uri.SetString(data_str.c_str(), data_str.length(), allocator);

    Value gltf_buffer(kObjectType);
    gltf_buffer.AddMember("uri", data_uri, allocator);
    gltf_buffer.AddMember("byteLength", size, allocator);

    auto &gltf_buffers = document_["buffers"];
    int buffer_index = gltf_buffers.Size();
    gltf_buffers.PushBack(gltf_buffer, allocator);

    // Create a glTF bufferView.
    Value gltf_bufferView(kObjectType);
    gltf_bufferView.AddMember("buffer", Value(buffer_index), allocator);
    gltf_bufferView.AddMember("byteOffset", Value(0), allocator);
    gltf_bufferView.AddMember("byteLength", size, allocator);
    gltf_bufferView.AddMember("target", is_vertex_buffer ? Value(34962) : Value(34963), allocator);
    if (is_vertex_buffer) {
      gltf_bufferView.AddMember("byteStride", mesh->attributes()->Get(0)->stride(), allocator);
    }

    auto &gltf_bufferViews = document_["bufferViews"];
    int buffer_view_index = gltf_bufferViews.Size();
    gltf_bufferViews.PushBack(gltf_bufferView, allocator);

    // Create a glTF accessor.
    Value gltf_accessor(kObjectType);
    gltf_accessor.AddMember("bufferView", Value(buffer_view_index), allocator);
    gltf_accessor.AddMember("byteOffset", Value(0), allocator);
    gltf_accessor.AddMember("componentType", is_vertex_buffer ? Value(5126) : Value(5125),
                            allocator);
    gltf_accessor.AddMember("count", count, allocator);
    gltf_accessor.AddMember("type", is_vertex_buffer ? Value("VEC3") : Value("SCALAR"), allocator);
    if (is_vertex_buffer) {
      Value gltf_max(kArrayType);
      gltf_max.PushBack(mesh->bbox_max()->x(), allocator);
      gltf_max.PushBack(mesh->bbox_max()->y(), allocator);
      gltf_max.PushBack(mesh->bbox_max()->z(), allocator);

      Value gltf_min(kArrayType);
      gltf_min.PushBack(mesh->bbox_min()->x(), allocator);
      gltf_min.PushBack(mesh->bbox_min()->y(), allocator);
      gltf_min.PushBack(mesh->bbox_min()->z(), allocator);

      gltf_accessor.AddMember("max", gltf_max, allocator);
      gltf_accessor.AddMember("min", gltf_min, allocator);
    }

    auto &gltf_accessors = document_["accessors"];
    int accessor_index = gltf_accessors.Size();
    gltf_accessors.PushBack(gltf_accessor, allocator);

    int texture_accessor_index = gltf_accessors.Size();

    // Add texture accessor for vertex buffer.
    if (is_vertex_buffer) {
      Value gltf_accessor(kObjectType);
      gltf_accessor.AddMember("bufferView", Value(buffer_view_index), allocator);
      gltf_accessor.AddMember("byteOffset", Value(8), allocator);
      gltf_accessor.AddMember("componentType", Value(5126), allocator);
      gltf_accessor.AddMember("count", count, allocator);
      gltf_accessor.AddMember("type", Value("VEC2"), allocator);

      Value gltf_max(kArrayType);
      gltf_max.PushBack(1.0, allocator);
      gltf_max.PushBack(1.0, allocator);

      Value gltf_min(kArrayType);
      gltf_min.PushBack(0.0, allocator);
      gltf_min.PushBack(0.0, allocator);

      gltf_accessor.AddMember("max", gltf_max, allocator);
      gltf_accessor.AddMember("min", gltf_min, allocator);

      gltf_accessors.PushBack(gltf_accessor, allocator);
    }

    // Add the accessor index to the glTF primitive.
    if (is_vertex_buffer) {
      auto gltf_attributes = Value(kObjectType);
      gltf_attributes.AddMember("POSITION", accessor_index, allocator);
      gltf_attributes.AddMember("TEXCOORD_0", texture_accessor_index, allocator);
      return gltf_attributes;
    } else {
      return Value(accessor_index);
    }
  }

  int glTF_export_material(const snapshot::Node *node) {
    auto &allocator = document_.GetAllocator();
    if (node->material_type() == snapshot::Material_Color) {
      auto color = static_cast<const snapshot::Color *>(node->material());
      Document gltf_material(kObjectType, &allocator);
      gltf_material.Parse(EMPTY_COLOR_MATERIAL);
      auto &gltf_color = gltf_material["pbrMetallicRoughness"]["baseColorFactor"];
      gltf_color.Clear();
      gltf_color.PushBack(color->red(), allocator);
      gltf_color.PushBack(color->green(), allocator);
      gltf_color.PushBack(color->blue(), allocator);
      gltf_color.PushBack(color->alpha(), allocator);

      auto &gltf_materials = document_["materials"];
      int materials_index = gltf_materials.Size();
      gltf_materials.PushBack(gltf_material, allocator);
      return materials_index;
    } else {
      auto image = static_cast<const snapshot::Image *>(node->material());

      // Convert raw image to png.
      const uint8_t *bytes = image->data()->Data();
      std::vector<uint8_t> out;
      if (!RawToPNG(image->width(), image->height(), bytes, out)) {
        FX_LOGS(FATAL) << "Unable to convert to PNG";
        return -1;
      }
      // Encode to base64.
      std::string base64_bytes = Base64Encode(out.data(), out.size());

      std::ostringstream data;
      data << "data:image/png;base64," << base64_bytes;

      auto data_str = data.str();
      Value data_uri(kStringType);
      data_uri.SetString(data_str.c_str(), data_str.length(), allocator);

      Value gltf_image(kObjectType);
      gltf_image.AddMember("mimeType", "image/png", allocator);
      gltf_image.AddMember("width", image->width(), allocator);
      gltf_image.AddMember("height", image->height(), allocator);
      gltf_image.AddMember("format", image->format(), allocator);
      gltf_image.AddMember("size", out.size(), allocator);
      gltf_image.AddMember("uri", data_uri, allocator);

      auto &gltf_images = document_["images"];
      int image_index = gltf_images.Size();
      gltf_images.PushBack(gltf_image, allocator);

      Value gltf_texture(kObjectType);
      gltf_texture.AddMember("sampler", 0, allocator);
      gltf_texture.AddMember("source", image_index, allocator);

      auto &gltf_textures = document_["textures"];
      int texture_index = gltf_textures.Size();
      gltf_textures.PushBack(gltf_texture, allocator);

      Document gltf_material(kObjectType, &allocator);
      gltf_material.Parse(EMPTY_TEXTURE_MATERIAL);
      auto &gltf_baseColorTexture = gltf_material["pbrMetallicRoughness"]["baseColorTexture"];
      gltf_baseColorTexture.AddMember("index", texture_index, allocator);

      auto &gltf_materials = document_["materials"];
      int materials_index = gltf_materials.Size();
      gltf_materials.PushBack(gltf_material, allocator);
      return materials_index;
    }
  }

 private:
  async::Loop *loop_;
  std::unique_ptr<sys::ComponentContext> context_;
  bool encountered_error_ = false;
  fuchsia::ui::scenic::ScenicPtr scenic_;
  fuchsia::ui::scenic::internal::SnapshotPtr snapshotter_;
  Document document_;
};

int main(int argc, const char **argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  const auto &positional_args = command_line.positional_args();
  if (positional_args.size() != 0) {
    FX_LOGS(ERROR) << "Usage: gltf_export\n"
                   << "Takes a snapshot in glTF format and writes it to stdout.\n"
                   << "To write to a file, redirect stdout, e.g.: "
                   << "gltf_export > \"${DST}\"";
    return 1;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());

  SnapshotTaker taker(&loop);
  taker.TakeSnapshot();
  loop.Run();

  return taker.encountered_error() ? EXIT_FAILURE : EXIT_SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// PNG encode.

bool RawToPNG(size_t width, size_t height, const uint8_t *data, std::vector<uint8_t> &out) {
  out.clear();

  png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  png_infop info_ptr = png_create_info_struct(png_ptr);

  if (setjmp(png_jmpbuf(png_ptr))) {
    png_destroy_write_struct(&png_ptr, NULL);
    FX_LOGS(ERROR) << "Unable to create write struct";
    return false;
  }

  png_set_IHDR(png_ptr, info_ptr, width, height, 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
               PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

  // Image snapshots are always in sRGB color space.
  png_set_sRGB(png_ptr, info_ptr, PNG_sRGB_INTENT_PERCEPTUAL);

  std::vector<uint8_t *> rows(height);
  for (size_t y = 0; y < height; ++y) {
    rows[y] = (uint8_t *)data + y * width * 4;
  }
  png_set_rows(png_ptr, info_ptr, &rows[0]);
  png_set_write_fn(
      png_ptr, &out,
      [](png_structp png_ptr, png_bytep data, png_size_t length) {
        std::vector<uint8_t> *p = (std::vector<uint8_t> *)png_get_io_ptr(png_ptr);
        p->insert(p->end(), data, data + length);
      },
      NULL);
  png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_BGR, NULL);

  if (setjmp(png_jmpbuf(png_ptr))) {
    png_destroy_write_struct(&png_ptr, NULL);
    FX_LOGS(ERROR) << "Unable to create write png";
    return false;
  }

  png_destroy_write_struct(&png_ptr, NULL);
  return true;
}

////////////////////////////////////////////////////////////////////////////////
// Base64 encode.

std::string Base64Encode(const uint8_t *bytes, size_t size) {
  std::string encoded(modp_b64_encode_len(size), '\0');
  size_t trim_begin = modp_b64_encode(const_cast<char *>(encoded.data()),
                                      reinterpret_cast<const char *>(bytes), size);
  encoded.erase(trim_begin, std::string::npos);
  return encoded;
}
