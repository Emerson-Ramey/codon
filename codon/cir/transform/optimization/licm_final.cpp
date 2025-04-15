#include "licm_f.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "codon/cir/analyze/dataflow/cfg.h"
#include "codon/cir/analyze/module/side_effect.h"
#include "codon/cir/transform/manager.h"
#include "codon/cir/util/cloning.h"
#include "codon/cir/util/irtools.h"
#include "codon/cir/util/iterators.h"

namespace codon {
namespace ir {
namespace transform {
namespace optimizations {

void LoopInvariantCodeMotion::run(Module *module) {
  auto *cfgAnalysis = getAnalysisResult<analyze::dataflow::CFAnalysis>(cfgAnalysisKey);
}

void LoopInvariantCodeMotion::findLoops(analyze::dataflow::CFGraph *cfg) {
  // cfg --> cfg blocks --> list of values
  // identify all loops in cfg and hoist instructions

}

void LoopInvariantCodeMotion::hoistLoopInstructions(ForFlow *v, const analyze::module::SideEffectAnalysis *seAnalysis) {
  // cast body of forflow to series flow --> list of values --> list of instructions
  // check if instruction has side effects
} 

bool LoopInvariantCodeMotion::isSafeToHoist(Value *val, 
    const analyze::module::SideEffectAnalysis *seAnalysis) {

}

lL

} // namespace optimization
} // namespace transform
} // namespace ir
} // namespace codon