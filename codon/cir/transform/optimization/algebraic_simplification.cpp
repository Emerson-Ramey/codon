#include "algebraic_simplification.h"

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

struct AlgebraicSimplification : public RewriteRule {

    bool constIsOne(Value *v) {
        if(util::isConst<int64_t>(v) && util::getConst<int64_t>(v) == 1) {
            return true;
        }
        return false;
    }

    bool constIsZero(Value *v) {
        if(util::isConst<int64_t>(v) && util::getConst<int64_t>(v) == 0) {
            return true;
        }
        return false;
    }


    void visit(CallInstr *v) override {
        auto *M = v->getModule();
        auto *fn = util::getFunc(v->getCallee());
        if(!fn) { return; }

        std::string op = fn->getUnmangledName();
        // only run if we have binary operation
        if(v->numArgs() == 2) {
            auto *lhs = v->front();
            auto *rhs = v->back();
            
            if (op == Module::MUL_MAGIC_NAME) {
                // x * 1 --> x
                if(constIsOne(lhs)) {
                    return setResult(rhs);
                }
                if(constIsOne(rhs)) {
                    return setResult(lhs);
                }
                // x * 0 --> 0
                if(constIsZero(lhs) || constIsZero(rhs)) {
                    return setResult(M->getInt(0));
                }
            }
            
             // x + 0 --> x
            if (op == Module::ADD_MAGIC_NAME) {
                if(constIsZero(lhs)) {
                    return setResult(rhs);
                }
                if(constIsZero(rhs)) {
                    return setResult(lhs);
                }
            }

            if (op == Module::SUB_MAGIC_NAME) {
                if ((constIsZero(rhs) && constIsZero(lhs)) || (constIsOne(rhs) && constIsOne(lhs))) {
                    return setResult(M->getInt(0));
                }

                // x - 0 --> x
                if(constIsZero(rhs)) {
                    return setResult(lhs);
                }

                // x - x --> 0
                if(util::match(lhs, rhs, false, true)) {
                    return setResult(M->getInt(0));
                }
            }

            if (op == Module::TRUE_DIV_MAGIC_NAME) {
                // x / 1 --> x
                if(constIsOne(rhs)) {
                    return setResult(lhs);
                }
                // 0 / x --> 0
                if(constIsZero(lhs) && !constIsZero(rhs)) {
                    return setResult(M->getInt(0));
                }
            }
        }
    }
};
} // namespace

const std::string AlgebraicSimplificationPass::KEY = "core-optimization-algebraic-simplification";

void AlgebraicSimplificationPass::run(Module *m) {
    registerStandardRules(m);
    Rewriter::reset();
    OperatorPass::run(m);
}

void AlgebraicSimplificationPass::handle(CallInstr *v) {
    auto *r = getAnalysisResult<analyze::module::SideEffectResult>(sideEffectsKey);
    if(!r->hasSideEffect(v)){
        rewrite(v);
    }
}

void AlgebraicSimplificationPass::handle(SeriesFlow *v) {
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

void AlgebraicSimplificationPass::registerStandardRules(Module *m) {
    registerRule("algebraic-simplification", std::make_unique<AlgebraicSimplification>());
}
}
}
}
}