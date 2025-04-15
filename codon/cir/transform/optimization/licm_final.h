// Copyright (C) 2022-2025 Exaloop Inc. <https://exaloop.io>

#pragma once

#include "codon/cir/analyze/dataflow/cfg.h"
#include "codon/cir/transform/pass.h"
#include "codon/cir/types/types.h"

#include <functional>
#include <memory>
#include <vector>

namespace codon {
namespace ir {
namespace transform {
namespace optimizations {

/// NumPy operator fusion pass.
class LICMPass : public Pass {
private:
  /// Key of the cfg analysis
  std::string cfAnalysisKey;

  /// Find natural loops in a function
  void findLoops(analyze::dataflow::CFGraph *cfg);

  

public:
  /// Constructs a licm pass.
  /// @param cfAnalysisKey the cfg analysis' key
  LoopInvariantCodeMotion(const std::string &cfAnalysisKey)
      : Pass(), cfAnalysisKey(cfAnalysisKey) {
  }

  std::string getKey() const override { return "opt-licm"; }

  void run(Module *module) override;
};
};
};
};

} // namespace optimizations
} // namespace transform
} // namespace ir
} // namespace codon
