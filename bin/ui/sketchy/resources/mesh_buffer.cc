#include "garnet/bin/ui/sketchy/resources/mesh_buffer.h"
#include "lib/ui/scenic/fidl_helpers.h"

namespace {

constexpr vk::DeviceSize kInitVertexBufferSize = 8192;
constexpr vk::DeviceSize kInitIndexBufferSize = 4096;
constexpr vk::DeviceSize kVertexStride = sizeof(float) * 4;
constexpr vk::DeviceSize kIndexStride = sizeof(uint32_t);

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
  auto bb_min = bbox_.min();
  auto bb_max = bbox_.max();
  float bb_min_arr[] = {bb_min.x, bb_min.y, bb_min.z};
  float bb_max_arr[] = {bb_max.x, bb_max.y, bb_max.z};
  scenic_mesh->BindBuffers(
      index_buffer_->scenic_buffer(),
      kMeshIndexFormat, 0 /* index_offset */, index_count_,
      vertex_buffer_->scenic_buffer(),
      scenic_lib::NewMeshVertexFormat(
          kMeshVertexPositionType,
          kMeshVertexNormalType,
          kMeshVertexTexCoodType),
      0 /* vertex_offset */, vertex_count_,
      bb_min_arr, bb_max_arr);
}

void MeshBuffer::Reset() {
  bbox_ = escher::BoundingBox();
  vertex_count_ = 0;
  index_count_ = 0;
}

std::pair<escher::BufferPtr, escher::BufferPtr> MeshBuffer::Preserve(
    escher::impl::CommandBuffer* command, escher::BufferFactory* factory,
    uint32_t vertex_count, uint32_t index_count,
    const escher::BoundingBox& bbox) {
  vk::DeviceSize vertex_size = kVertexStride * vertex_count;
  vk::DeviceSize index_size = kIndexStride * index_count;
  vertex_count_ += vertex_count;
  index_count_ += index_count;
  bbox_.Join(bbox);
  return {vertex_buffer_->PreserveBuffer(command, factory, vertex_size),
          index_buffer_->PreserveBuffer(command, factory, index_size)};
};

}  // namespace sketchy_service
