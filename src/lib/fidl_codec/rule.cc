// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fidl_codec/rule.h"

namespace fidl_codec {

void HandleExpression::Visit(ExpressionVisitor* visitor) const {
  visitor->VisitHandleExpression(this);
}

void MessageExpression::Visit(ExpressionVisitor* visitor) const {
  visitor->VisitMessageExpression(this);
}

void DescriptionExpression::Visit(ExpressionVisitor* visitor) const {
  visitor->VisitDescriptionExpression(this);
}

void AccessExpression::Visit(ExpressionVisitor* visitor) const {
  visitor->VisitAccessExpression(this);
}

void DivExpression::Visit(ExpressionVisitor* visitor) const {
  visitor->VisitDivExpression(this);
}

}  // namespace fidl_codec
