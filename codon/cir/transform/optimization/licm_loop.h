// Copyright (C) 2022-2025 Exaloop Inc. <https://exaloop.io>

#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "codon/cir/transform/pass.h"
#include "codon/cir/analyze/dataflow/dominator.h"
#include "codon/cir/analyze/dataflow/cfg.h"
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
  
  /// Key for the CFG analysis
  std::string cfgAnalysisKey;
  
  /// Key for the side effect analysis
  std::string seAnalysisKey;

  /// Representation of a natural loop
  struct Loop {
    BasicBlock *header;
    std::unordered_set<BasicBlock *> blocks;
    BasicBlock *preheader;
  };

public:
  /// @param domAnalysisKey the dominator analysis key
  /// @param cfgAnalysisKey the CFG analysis key
  /// @param seAnalysisKey the side effect analysis key
  LoopInvariantCodeMotion(const std::string &domAnalysisKey,
                          const std::string &cfgAnalysisKey,
                          const std::string &seAnalysisKey)
      : domAnalysisKey(domAnalysisKey), cfgAnalysisKey(cfgAnalysisKey), 
        seAnalysisKey(seAnalysisKey) {}

  std::string getKey() const override { return "opt-licm"; }
  
  void run(Module *module) override;
  
private:
  /// Find natural loops in a function
  std::vector<Loop> findNaturalLoops(BodiedFunc *func, 
                                     analyze::dataflow::CFGraph *cfg,
                                     analyze::dataflow::DominatorAnalysis *domAnalysis);
  
  /// Find a loop preheader or create one if it doesn't exist
  BasicBlock *getOrCreatePreheader(Loop &loop, BodiedFunc *func);
  
  /// Check if an instruction is loop invariant
  bool isLoopInvariant(Value *val, const std::unordered_set<Value *> &loopVars);
  
  /// Check if it's safe to hoist an instruction outside its loop
  bool isSafeToHoist(Value *val, const analyze::module::SideEffectAnalysis *seAnalysis);
  
  /// Perform LICM on a loop
  bool performLICM(Loop &loop, BodiedFunc *func,
                   analyze::dataflow::DominatorAnalysis *domAnalysis,
                   analyze::module::SideEffectAnalysis *seAnalysis);
};

} // namespace optimization
} // namespace transform
} // namespace ir
} // namespace codon