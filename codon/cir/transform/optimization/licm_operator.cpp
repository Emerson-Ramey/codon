#include "licm_operator.h"

#include <unordered_set>
#include <vector>
#include <iterator>
#include <utility>

#include "codon/cir/analyze/module/side_effect.h"
#include "codon/cir/analyze/dataflow/dominator.h"
#include "codon/cir/util/visitor.h"
#include "codon/cir/util/iterators.h"


namespace codon {
namespace ir {
namespace transform {
namespace optimizations {

const std::string LICMPass::KEY = "core-licm";
      
void LICMPass::run(Module *m) {
  OperatorPass::run(m);
}


// HANDLE functions for different flow types
void LICMPass::handle(ForFlow *v) {
  auto *body = cast<SeriesFlow>(v->getBody());
  if (!body) {
    return;
  }
  
  // create preheader
  auto *parent = M->N<SeriesFlow>(v->getSrcInfo());

  performCodeMotion(v, body, parent);
  // add back original loop with expression removed
  parent->push_back(v)
  // replace the original loop with new "preheader + (original loop - exprs)" series
  v->replaceAll(parent)
}

void LICMPass::handle(ImperativeForFlow *v) {
  auto *body = cast<SeriesFlow>(v->getBody());
  if (!body) {
    return;
  }
  
  // create preheader
  auto *parent = M->N<SeriesFlow>(v->getSrcInfo());

  performCodeMotion(v, body, parent);
  parent->push_back(v)
  v->replaceAll(parent)
}

void LICMPass::handle(WhileFlow *v) {
  auto *body = cast<SeriesFlow>(v->getBody());
  if (!body) {
    return;
  }
  
  // create preheader
  auto *parent = M->N<SeriesFlow>(v->getSrcInfo());

  performCodeMotion(v, body, parent);
  parent->push_back(v)
  v->replaceAll(parent)
}

// HELPER FUNCTIONS
template <typename T>
void LICMPass::performCodeMotion(T *loop, SeriesFlow *body, SeriesFlow *parent) {  
  // Identify invariant expressions
  std::vector<Value *> invariantExprs;
  for (auto it = body->begin(); it != body->end(); ) {
    Value *expr = *it;
    
    if (isLoopInvariant(expr, loop)) {
      // Found an invariant expression
      invariantExprs.push_back(expr);
      // Remove it from the loop body
      it = body->erase(it);
    } else {
      ++it;
    }
  }
  
  // Insert invariant expressions before the loop
  for (auto *expr : invariantExprs) {
    parent->push_back(expr);
  }
}

template <typename T>
bool LICMPass::isLoopInvariant(Value *expr, T *loop) {
  // Check if expression has side effects
  auto *sideEffects = getAnalysisResult<analyze::module::SideEffectResult>(sideEffectsKey);
  if (auto *call = cast<CallInstr>(expr)) {
    if (sideEffects->hasSideEffect(call))
      return false;
  }

  // Check for memory instructions
  if (isa<AssignInstr>(expr) || isa<ExtractInstr>(expr) || isa<StackAllocInstr>(expr) || isa<InsertInstr>(expr)) {
    return false;
  }

  // Check for control flow instructions
  if (isa<BranchInstr>(expr) || isa<ReturnInstr>(expr) || isa<YieldInstr>(expr)|| isa<YieldInstr>(ThrowInstr)) {
    return false;
  }

  // Collect variables modified in the loop
  auto loopModifiedVars = collectModifiedVars(loop);
  
  // Check if expression uses any variables modified in the loop
  for (auto *var : getUsedVars(expr)) {
    if (loopModifiedVars.count(var) > 0)
      return false;
  }

  // If we reach here, expression is invariant
  return true;
}

template <typename T>
std::unordered_set<Var *> LICMPass::collectModifiedVars(T *loop) {
  std::unordered_set<Var *> modified;
  
  class ModifiedVarsVisitor : public util::Visitor {
  private:
    std::unordered_set<Var *> &vars;
    
  public: 
    ModifiedVarsVisitor(std::unordered_set<Var *> &vars) : vars(vars) {}
    
    void visit(AssignInstr *v) override {
      if (auto *var = cast<Var>(v->getLhs()))
        vars.insert(var);
    }
        
    // Add other operations that might modify variables
  };
  
  ModifiedVarsVisitor visitor(modified);
  visitor.process(loop);
  
  return modified;
}

std::vector<Var *> LICMPass::getUsedVars(Value *expr) {
  std::vector<Var *> used;
  
  class UsedVarsVisitor : public util::Visitor {
  private:
    std::vector<Var *> &vars;
    std::unordered_set<Var *> seen;
    
  public:
    UsedVarsVisitor(std::vector<Var *> &vars) : vars(vars) {}
    
    void visit(VarValue *v) override {
      auto *var = v->getVar();
      if (seen.insert(var).second)
        vars.push_back(var);
    }
  };
  
  UsedVarsVisitor visitor(used);
  visitor.process(expr);
  
  return used;
}



} // namespace optimizations
} // namespace transform
} // namespace ir
} // namespace codon