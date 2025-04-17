#include "strength_reduction.h"

#include <algorithm>
#include <functional>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <cmath>

#include "codon/cir/analyze/module/side_effect.h"
#include "codon/cir/transform/rewrite.h"
#include "codon/cir/util/irtools.h"
#include "codon/cir/util/matching.h"

namespace codon {
namespace ir {
namespace transform {
namespace optimizations {
namespace {

bool isPowerOfTwo(int64_t x) {
  return x > 0 && (x & (x - 1)) == 0;
}

int64_t log2_int(int64_t value) {
  if (value <= 0) {
    throw std::invalid_argument("log2 is undefined for non-positive integers.");
  }
  int result = 0;
  while(value >>= 1) {
    ++result;
  }

  return result;
}

// x * C -> x << C or C * x -> x << C
struct StrengthReduction : public RewriteRule {
  void visit(CallInstr *v) override {
    auto *M = v->getModule();
    auto *type = v->getType();

    if (!util::isCallOf(v, Module::IMUL_MAGIC_NAME, 2, /*output=*/nullptr,
                        /*method=*/true))
      return;

    Value *lhs = v->front();
    Value *rhs = v->back();

    if (!lhs->getType()->is(rhs->getType()))
      return;

    // Check if C or x is both an integer and log2 number, return if not
    if (!util::isConst<int64_t>(rhs) || !util::isConst<int64_t>(lhs)) {
        return;
    }

    bool lhs_valid = isPowerOfTwo(util::getConst<int64_t>(lhs));
    bool rhs_valid = isPowerOfTwo(util::getConst<int64_t>(rhs));
    if (!lhs_valid && !rhs_valid) {
        return;
    }

    int64_t c; 

    if (rhs_valid) {
      c = util::getConst<int64_t>(rhs);
    } else {
      c = util::getConst<int64_t>(lhs);
    }

    int64_t log2_c = log2_int(c);

    Value *newCall = nullptr;
    
    if (c != -(1ull << 63)) // ensure no overflow
        newCall = *rhs << *(M->getInt(log2_c));
    

    if (newCall && newCall->getType()->is(type) &&
        !util::match(v, newCall, /*checkNames=*/false, /*varIdMatch=*/true))
      return setResult(newCall);
  }
};
} // namespace

const std::string StrengthReductionPass::KEY = "core-optimization-strength-reduction";

void StrengthReductionPass::run(Module *m) {
  registerStandardRules(m);
  Rewriter::reset();
  OperatorPass::run(m);
}

void StrengthReductionPass::handle(CallInstr *v) {
  auto *r = getAnalysisResult<analyze::module::SideEffectResult>(sideEffectsKey);
  if (!r->hasSideEffect(v))
    rewrite(v);
}

void StrengthReductionPass::handle(SeriesFlow *v) {
  auto it = v->begin();
  while (it != v->end()) {
    if (auto *series = cast<SeriesFlow>(*it)) {
      it = v->erase(it);
      for (auto *x : *series) {
        it = v->insert(it, x);
        ++it;
      }
    } else if (auto *flowInstr = cast<FlowInstr>(*it)) {
      it = v->erase(it);
      // inserting in reverse order causes [flow, value] to be added
      it = v->insert(it, flowInstr->getValue());
      it = v->insert(it, flowInstr->getFlow());
      // don't increment; re-traverse in case a new series flow added
    } else {
      ++it;
    }
  }
}

void StrengthReductionPass::registerStandardRules(Module *m) {
  registerRule("strength-reduction", std::make_unique<StrengthReduction>());
}

} // namespace optimizations
} // namespace transform
} // namespace ir
} // namespace codon
