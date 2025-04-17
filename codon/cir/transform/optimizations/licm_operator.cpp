#include "licm_operator.h"

#include <unordered_set>
#include <vector>
#include <iterator>
#include <utility>
#include <iostream>

#include "codon/cir/analyze/module/side_effect.h"
#include "codon/cir/util/visitor.h"
#include "codon/cir/util/iterators.h"
#include "codon/cir/util/cloning.h"


namespace codon {
namespace ir {
namespace transform {
namespace optimizations {

const std::string LICMPass::KEY = "core-licm";
      
void LICMPass::run(Module *m) {
  OperatorPass::run(m);
}


// // HANDLE functions for different flow types
// void LICMPass::handle(ForFlow *v) {
//   auto *M = v->getModule();
//   std::cout << "LICM for flow " << *v << "\n";
//   auto *body = cast<SeriesFlow>(v->getBody());
//   if (!body) {
//     return;
//   }
  
//   // create preheader
//   auto *parent = M->N<SeriesFlow>(v);

//   if (performCodeMotion(v, body, parent)) {
//     std::cout << "performCodeMotion done" << "\n";
//     parent->push_back(v);
//     std::cout << "pushed back loop to new series" << "\n";
//     v->replaceAll(parent);  // replace the original loop with new "preheader + (original loop - exprs)" series
//     std::cout << "replaced all" << "\n";  
//   }
  
  
// }
// void LICMPass::handle(WhileFlow *v) {
//   auto *M = v->getModule();
//   std::cout << "LICM while flow " << v << "\n";
//   auto *body = cast<SeriesFlow>(v->getBody());
//   if (!body) {
//     return;
//   }
  
//   // create preheader
//   auto *parent = M->N<SeriesFlow>(v);

//   performCodeMotion(v, body, parent);
//   std::cout << "performCodeMotion done" << "\n";
//   parent->push_back(v);
//   std::cout << "pushed back loop to new series" << "\n";
//   v->replaceAll(parent);
//   std::cout << "replace all" << "\n";
// }

void LICMPass::handle(ImperativeForFlow *v) {
  auto *M = v->getModule();
  util::CloneVisitor cv(M);

  std::cout << "LICM imperative for flow " << *v << "\n";
  auto *body = cast<SeriesFlow>(v->getBody());
  if (!body) {
    return;
  }
  
  // create preheader
  auto *parent = M->N<SeriesFlow>(v);

  if (performCodeMotion(v, body, parent)) {
    std::cout << "performCodeMotion done" << "\n";
    parent->push_back(cv.clone(v));
    std::cout << "pushed back loop to new series" << "\n";
    v->replaceAll(parent);
    std::cout << "replaced all" << *v << "\n";  
  }
}

// HELPER FUNCTIONS
template <typename T>
bool LICMPass::performCodeMotion(T *loop, SeriesFlow *body, SeriesFlow *parent) {  
  std::cout << "LICM perform code motion "<< "\n";
  // Identify invariant expressions
  std::vector<Value *> invariantExprs;
  for (auto it = body->begin(); it != body->end(); ) {
    Value *expr = *it;
    std::cout << "EXPRESSION (" << *(expr->getType()) << ") " << *expr << "\n";
    
    if (isLoopInvariant(expr, loop)) {
      // Found an invariant expression
      invariantExprs.push_back(expr);
      std::cout << "pushed back invariant expr" << "\n";
      // Remove it from the loop body
      it = body->erase(it);
      std::cout << "erased invariant expr" << "\n";
    } else {
      ++it;
    }
  }
  std::cout << "loop through body done" << "\n";
  // Insert invariant expressions before the loop
  bool invariant_exists = false;
  for (auto *expr : invariantExprs) {
    // parent->push_back(expr);
    invariant_exists = true;
    parent->insert(parent->begin(), expr);
    std::cout << "inserted " << *expr << "\n";
  }
  std::cout << "done done with all " << "\n";
  return invariant_exists;
}

template <typename T>
bool LICMPass::isLoopInvariant(Value *expr, T *loop) {
  std::cout << "LICM isLoopInvariant " << "\n";
  // Check if expression has side effects
  auto *sideEffects = getAnalysisResult<analyze::module::SideEffectResult>(sideEffectsKey);
  if (auto *call = cast<CallInstr>(expr)) {
    if (sideEffects->hasSideEffect(call))
      return false;
  }

  // Check for memory instructions
  if (isA<ExtractInstr>(expr) || isA<StackAllocInstr>(expr) || isA<InsertInstr>(expr)) {
    return false;
  }

  // Check for control flow instructions
  if (isA<ControlFlowInstr>(expr) || isA<ReturnInstr>(expr) || isA<YieldInstr>(expr)|| isA<YieldInInstr>(expr) || isA<BreakInstr>(expr) || isA<ContinueInstr>(expr) || isA<ThrowInstr>(expr)) {
    return false;
  }

  // Collect variables modified in the loop
  auto loopModifiedVars = collectModifiedVars(loop);
  
  // Check if expression uses any variables modified in the loop
  std::cout << "check if uses variables modified" << "\n";
  for (auto *var : expr->getUsedVariables()) {
    std::cout << *var << "\n";
    if (loopModifiedVars.count(var) > 0)
      return false;
  }
  std::cout << "LICM found invariant expression" << "\n";
  // If we reach here, expression is invariant
  return true;
}

template <typename T>
std::unordered_set<Var *> LICMPass::collectModifiedVars(T *loop) {
  std::cout << "LICM starting collectModifiedVars" << "\n";
  std::unordered_set<Var *> modified;
  
  class ModifiedVarsVisitor : public util::Visitor {
  private:
    std::unordered_set<Var *> &vars;
    
  public: 
    ModifiedVarsVisitor(std::unordered_set<Var *> &vars) : vars(vars) {}
    
    void visit(Value *v) override {
      if (auto *assign = cast<AssignInstr>(v)) {
        if (auto *var = cast<Var>(assign->getLhs())) {
          vars.insert(var);
          std::cout << *var << "\n";
        }
      }
    }
      
  };
  
  ModifiedVarsVisitor visitor(modified);
  visitor.visit(loop);
  std::cout << "done collectModifiedVars" << "\n";
  return modified;
}


} // namespace optimizations
} // namespace transform
} // namespace ir
} // namespace codon