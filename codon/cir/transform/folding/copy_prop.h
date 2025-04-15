// Copyright (C) 2022-2025 Exaloop Inc. <https://exaloop.io>

#pragma once

#include "codon/cir/transform/pass.h"

namespace codon {
namespace ir {
namespace transform {
namespace folding {

/// Copy propagation pass.
class CopyPropPass : public OperatorPass {
private:
  /// Key of the reaching definition analysis
  std::string reachingDefKey;
  /// Key of the global variables analysis
  std::string globalVarsKey;

public:
  static const std::string KEY;

  /// Constructs a copy propagation pass.
  /// @param reachingDefKey the reaching definition analysis' key
  /// @param globalVarsKey global variables analysis' key
  CopyPropPass(const std::string &reachingDefKey, const std::string &globalVarsKey)
      : reachingDefKey(reachingDefKey), globalVarsKey(globalVarsKey) {}

  std::string getKey() const override { return KEY; }
  void handle(VarValue *v) override;
};

} // namespace folding
} // namespace transform
} // namespace ir
} // namespace codon
