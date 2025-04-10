// Copyright (C) 2022-2025 Exaloop Inc. <https://exaloop.io>

#pragma once

#include "codon/cir/transform/pass.h"
#include "codon/cir/analyze/dataflow/dominator.h"
#include "codon/cir/analyze/module/side_effect.h"

namespace codon {
namespace ir {
namespace transform {
namespace optimization {

/**
 * A pass that performs Loop Invariant Code Motion.
 * This optimization identifies computations inside loops that produce the same result
 * on every iteration and hoists them outside the loop.
 */
class LoopInvariantCodeMotion : public Pass {
private:
  /// Key for the dominator analysis
  std::string domAnalysisKey;
  
  /// Key for the side effect analysis
  std::string seAnalysisKey;

public:
  /// @param domAnalysisKey the dominator analysis key
  /// @param seAnalysisKey the side effect analysis key
  LoopInvariantCodeMotion(const std::string &domAnalysisKey, 
                          const std::string &seAnalysisKey)
      : domAnalysisKey(domAnalysisKey), seAnalysisKey(seAnalysisKey) {}

  std::string getKey() const override { return "opt-licm"; }
  
  void run(Module *module) override;
  
private:
  /// Check if an instruction is loop invariant
  bool isLoopInvariant(Value *val, const std::set<Value *> &loopVars);
  
  /// Check if it's safe to move an instruction outside its loop
  bool isSafeToHoist(Value *val, const analyze::module::SideEffectAnalysis *seAnalysis);
  
  /// Hoist loop-invariant instructions out of the loop
  void hoistInstructions(Value *loopHeader, 
                          const std::vector<Value *> &invariantInsts,
                          const analyze::dataflow::DominatorAnalysis *domAnalysis);
};

} // namespace optimization
} // namespace transform
} // namespace ir
} // namespace codon