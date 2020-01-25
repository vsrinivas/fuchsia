// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_SNAPSHOT_SNAPSHOTTER_H_
#define SRC_UI_SCENIC_LIB_GFX_SNAPSHOT_SNAPSHOTTER_H_

#include <fuchsia/mem/cpp/fidl.h>
#include <lib/fit/function.h>

#include "src/ui/lib/escher/renderer/batch_gpu_downloader.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/shape/rounded_rect.h"
#include "src/ui/scenic/lib/gfx/resources/resource_visitor.h"
#include "src/ui/scenic/lib/gfx/snapshot/serializer.h"

namespace scenic_impl {
namespace gfx {

class Node;
class Resource;

// Callback for the snapshotter, where the buffer stores the snapshot data and
// the bool represents the success or lack thereof of the snapshot operation.
using TakeSnapshotCallback = fit::function<void(fuchsia::mem::Buffer, bool)>;

// Defines a |ResourceVisitor| that takes snapshot of a branch of the scene
// graph. It provides the snapshot in flatbuffer formatted |fuchsia.mem.Buffer|.
// It uses |Serializer| set of classes to recreate the node hierarchy while
// visiting every entity of the scenic node. After the visit, the serializer
// generates the flatbuffer in |TakeSnapshot|.
class Snapshotter : public ResourceVisitor {
 public:
  Snapshotter(escher::EscherWeakPtr escher);
  virtual ~Snapshotter() = default;

  // Takes the snapshot of the |node| and calls the |callback| with a
  // |fuchsia.mem.Buffer| buffer.
  void TakeSnapshot(Resource* resource, TakeSnapshotCallback snapshot_callback);

 protected:
  void Visit(Memory* r) override;
  void Visit(Image* r) override;
  void Visit(ImagePipeBase* r) override;
  void Visit(Buffer* r) override;
  void Visit(View* r) override;
  void Visit(ViewNode* r) override;
  void Visit(ViewHolder* r) override;
  void Visit(EntityNode* r) override;
  void Visit(OpacityNode* r) override;
  void Visit(ShapeNode* r) override;
  void Visit(Scene* r) override;
  void Visit(CircleShape* r) override;
  void Visit(RectangleShape* r) override;
  void Visit(RoundedRectangleShape* r) override;
  void Visit(MeshShape* r) override;
  void Visit(Material* r) override;
  void Visit(Compositor* r) override;
  void Visit(DisplayCompositor* r) override;
  void Visit(LayerStack* r) override;
  void Visit(Layer* r) override;
  void Visit(Camera* r) override;
  void Visit(Renderer* r) override;
  void Visit(Light* r) override;
  void Visit(AmbientLight* r) override;
  void Visit(DirectionalLight* r) override;
  void Visit(PointLight* r) override;

 private:
  // Struct for a vertex that contains interwoven
  // position and uv data.
  struct PosUvVertex {
    escher::vec2 pos;
    escher::vec2 uv;
  };

  // Used to keep alive RoundedRectData until serialization is complete.
  struct RoundedRectData {
    std::vector<uint32_t> indices;
    std::vector<PosUvVertex> vertices;
  };

  void VisitNode(Node* r);
  void VisitResource(Resource* r);
  void VisitMesh(escher::MeshPtr mesh);
  void VisitImage(escher::ImagePtr i);
  void VisitRoundedRectSpec(const escher::RoundedRectSpec& spec);

  void ReadImage(const escher::ImagePtr& image, escher::BatchGpuDownloader::Callback callback);
  void ReadBuffer(const escher::BufferPtr& buffer, escher::BatchGpuDownloader::Callback callback);

  // Create a replacement image for cases when when can't read protected memory.
  escher::ImagePtr CreateReplacementImage(uint32_t width, uint32_t height);
  std::unique_ptr<escher::BatchGpuUploader> gpu_uploader_for_replacements_;

  std::unique_ptr<escher::BatchGpuDownloader> gpu_downloader_;
  const escher::EscherWeakPtr escher_;

  // Holds the current serializer for the scenic node being serialized. This is
  // needed when visiting node's content like mesh, material and images.
  std::shared_ptr<NodeSerializer> current_node_serializer_;

  // Vector of all visited rounded rects' data.
  std::vector<RoundedRectData> rounded_rect_data_vec_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Snapshotter);
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_SNAPSHOT_SNAPSHOTTER_H_
