#pragma once

#include "codon/cir/transform/pass.h"
#include "codon/cir/transform/rewrite.h"

namespace codon {
namespace ir {
namespace transform {
namespace optimizations {

/// Strength Reduction pass that converts log base 2 multiplications
/// with left shifts. Ex. x * 4 -> x << 2
class StrengthReductionPass : public OperatorPass, public Rewriter {
private:
  std::string sideEffectsKey;

public:
  /// Constructs a canonicalization pass
  /// @param sideEffectsKey the side effect analysis' key
  StrengthReductionPass(const std::string &sideEffectsKey)
      : OperatorPass(/*childrenFirst=*/true), sideEffectsKey(sideEffectsKey) {}

  static const std::string KEY;
  std::string getKey() const override { return KEY; }

  void run(Module *m) override;
  void handle(CallInstr *) override;
  void handle(SeriesFlow *) override;

private:
  void registerStandardRules(Module *m);
};

} // namespace cleanup
} // namespace transform
} // namespace ir
} // namespace codon
