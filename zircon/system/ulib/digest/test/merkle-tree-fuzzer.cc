#include <zircon/status.h>

#include <vector>

#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <digest/node-digest.h>
#include <fuzzer/FuzzedDataProvider.h>
#include <sanitizer/asan_interface.h>

// The only valid node sizes are the powers of 2 between 512 and 32768.
constexpr size_t kValidNodeSizes[] = {
    1 << 9, 1 << 10, 1 << 11, 1 << 12, 1 << 13, 1 << 14, 1 << 15,
};
static_assert(kValidNodeSizes[0] == digest::kMinNodeSize);
static_assert(kValidNodeSizes[std::size(kValidNodeSizes) - 1] == digest::kMaxNodeSize);

// Restrict the amount of data that a Merkle tree is generated for to 16MiB.
// The minimum node size is 512 bytes which can hold 16 hashes.  An input of 16MiB will create a
// Merkle tree with 4 levels plus the root for the minimum node size which should be enough to
// exercise all of the Merkle tree code.
constexpr size_t kMaxBufLen = 1 << 24;

void AssertOk(zx_status_t status) {
  ZX_ASSERT_MSG(status == ZX_OK, "Expected: ZX_OK, got: %s", zx_status_get_string(status));
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider provider(data, size);

  // The execution flow of the Merkle tree code only depends on the buffer size, node size, and if
  // the compact format is used.  The contents of the buffer are only looked at by the SHA256 code
  // and have no affect on the execution of the Merkle tree code.  For this reason the contents of
  // the buffer are not fuzzed.
  std::vector<uint8_t> buffer(provider.ConsumeIntegralInRange<size_t>(0, kMaxBufLen), 0);
  size_t node_size = provider.PickValueInArray(kValidNodeSizes);
  bool use_compact_format = provider.ConsumeBool();

  digest::MerkleTreeCreator creator;
  creator.SetNodeSize(node_size);
  creator.SetUseCompactFormat(use_compact_format);
  AssertOk(creator.SetDataLength(buffer.size()));
  std::vector<uint8_t> tree(creator.GetTreeLength());
  std::vector<uint8_t> root(digest::kSha256Length);
  AssertOk(creator.SetTree(tree.data(), tree.size(), root.data(), root.size()));
  AssertOk(creator.Append(buffer.data(), buffer.size()));

  digest::MerkleTreeVerifier verifier;
  verifier.SetNodeSize(node_size);
  verifier.SetUseCompactFormat(use_compact_format);
  AssertOk(verifier.SetDataLength(buffer.size()));
  ZX_ASSERT(tree.size() == verifier.GetTreeLength());
  AssertOk(verifier.SetTree(tree.data(), tree.size(), root.data(), root.size()));
  // Verify all of the data.
  AssertOk(verifier.Verify(buffer.data(), buffer.size(), /*data_off=*/0));

  // Verify a portion of the data.
  size_t verify_offset = provider.ConsumeIntegralInRange<size_t>(0, buffer.size());
  size_t verify_len = provider.ConsumeIntegralInRange<size_t>(0, buffer.size() - verify_offset);
  AssertOk(verifier.Align(&verify_offset, &verify_len));
  // Poison all of the data then unpoison only the section that is being verified.
  ASAN_POISON_MEMORY_REGION(buffer.data(), buffer.size());
  ASAN_UNPOISON_MEMORY_REGION(buffer.data() + verify_offset, verify_len);
  AssertOk(verifier.Verify(buffer.data() + verify_offset, verify_len, verify_offset));
  // Unpoison all of the data.
  ASAN_UNPOISON_MEMORY_REGION(buffer.data(), buffer.size());

  // Check that the Merkle tree size calculations match.
  ZX_ASSERT(tree.size() ==
            digest::CalculateMerkleTreeSize(buffer.size(), node_size, use_compact_format));
  return 0;
}
