#ifndef GARNET_BIN_A11Y_A11Y_MANAGER_SEMANTICS_SEMANTIC_TREE_IMPL_H_
#define GARNET_BIN_A11Y_A11Y_MANAGER_SEMANTICS_SEMANTIC_TREE_IMPL_H_

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include "lib/fidl/cpp/binding_set.h"

namespace a11y_manager {
class SemanticTreeImpl
    : public fuchsia::accessibility::semantics::SemanticTree {
 public:
  explicit SemanticTreeImpl(zx::event view_ref)
      : view_ref_(std::move(view_ref)) {}

 private:
  // TODO(MI4-1736): Implement this based on intern code (semantic_tree.cc)

  // |fuchsia::accessibility::semantics::SemanticsTree|:
  void Commit() override;

  void UpdateSemanticNodes(
      std::vector<fuchsia::accessibility::semantics::Node> nodes) override;

  void DeleteSemanticNodes(std::vector<uint32_t> node_ids) override;

  zx::event view_ref_;
};
}  // namespace a11y_manager
#endif  // GARNET_BIN_A11Y_A11Y_MANAGER_SEMANTICS_SEMANTIC_TREE_IMPL_H_
