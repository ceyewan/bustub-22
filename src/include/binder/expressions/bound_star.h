#pragma once

#include "binder/bound_expression.h"
#include "common/exception.h"
#include <string>
#include <utility>

namespace bustub {

/**
 * The star in SELECT list, e.g. `SELECT * FROM x`.
 */
class BoundStar : public BoundExpression {
public:
  BoundStar() : BoundExpression(ExpressionType::STAR) {}

  auto HasAggregation() const -> bool override {
    throw bustub::Exception(
        "`HasAggregation` should not have been called on `BoundStar`.");
  }

  auto ToString() const -> std::string override { return "*"; }
};
} // namespace bustub
