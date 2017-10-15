#include "garnet/bin/ui/sketchy/resources/mesh_buffer.h"
#include "lib/ui/scenic/fidl_helpers.h"

namespace {

constexpr vk::DeviceSize kInitVertexBufferSize = 8192;
constexpr vk::DeviceSize kInitIndexBufferSize = 4096;

constexpr auto kMeshVertexPositionType = scenic::ValueType::kVector2;
constexpr auto kMeshVertexNormalType = scenic::ValueType::kNone;
constexpr auto kMeshVertexTexCoodType = scenic::ValueType::kVector2;
constexpr auto kMeshIndexFormat = scenic::MeshIndexFormat::kUint32;

}  // namespace

namespace sketchy_service {

MeshBuffer::MeshBuffer(scenic_lib::Session* session,
                       escher::BufferFactory* buffer_factory)
    : vertex_buffer_(Buffer::New(
          session, buffer_factory, BufferType::kVertex, kInitVertexBufferSize)),
      index_buffer_(Buffer::New(
          session, buffer_factory, BufferType::kIndex, kInitIndexBufferSize)) {}

void MeshBuffer::ProvideBuffersToScenicMesh(scenic_lib::Mesh* scenic_mesh) {
  auto bb_min = bounding_box_.min();
  auto bb_max = bounding_box_.max();
  float bb_min_arr[] = {bb_min.x, bb_min.y, bb_min.z};
  float bb_max_arr[] = {bb_max.x, bb_max.y, bb_max.z};

  scenic_mesh->BindBuffers(
      index_buffer_->scenic_buffer(),
      kMeshIndexFormat, 0 /* index_offset */, num_indices_,
      vertex_buffer_->scenic_buffer(),
      scenic_lib::NewMeshVertexFormat(
          kMeshVertexPositionType,
          kMeshVertexNormalType,
          kMeshVertexTexCoodType),
      0 /* vertex_offset */, num_vertices_,
      bb_min_arr, bb_max_arr);
}

}  // namespace sketchy_service
