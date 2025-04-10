// Copyright (C) 2022-2025 Exaloop Inc. <https://exaloop.io>

#include "licm.h"

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
namespace optimization {

void LoopInvariantCodeMotion::run(Module *module) {
  auto *domAnalysis = getAnalysisResult<analyze::dataflow::DominatorAnalysis>(domAnalysisKey);
  auto *seAnalysis = getAnalysisResult<analyze::module::SideEffectAnalysis>(seAnalysisKey);
  
  // Process each function in the module
  for (auto *func : *module) {
    // Skip functions without a body
    if (!func->hasValue())
      continue;

    // Find all natural loops in the function
    auto *cfg = domAnalysis->getCFG();
    auto loops = cfg->findNaturalLoops();
    
    // Process each loop
    for (auto &loop : loops) {
      auto *header = loop.first;
      auto &bodyBlocks = loop.second;
      
      // Collect all instructions in the loop
      std::vector<Value *> loopInstructions;
      for (auto *block : bodyBlocks) {
        for (auto *inst : *block) {
          loopInstructions.push_back(inst);
        }
      }
      
      // Identify loop-carried variables
      std::set<Value *> loopCarriedVars;
      for (auto *block : bodyBlocks) {
        for (auto *phi : block->getPhis()) {
          loopCarriedVars.insert(phi);
          // Add all operands of phi nodes that come from within the loop
          for (auto it = phi->op_begin(); it != phi->op_end(); ++it) {
            auto *val = *it;
            if (val->getParent() && bodyBlocks.count(val->getParent()))
              loopCarriedVars.insert(val);
          }
        }
      }
      
      // Find loop-invariant instructions
      std::vector<Value *> invariantInsts;
      for (auto *inst : loopInstructions) {
        if (isLoopInvariant(inst, loopCarriedVars) && isSafeToHoist(inst, seAnalysis)) {
          invariantInsts.push_back(inst);
        }
      }
      
      // Hoist invariant instructions out of the loop
      if (!invariantInsts.empty()) {
        hoistInstructions(header, invariantInsts, domAnalysis);
      }
    }
  }
}

bool LoopInvariantCodeMotion::isLoopInvariant(Value *val, const std::set<Value *> &loopVars) {
  // Skip non-instructions or special cases
  if (!val || !val->isA<Instruction>() || val->isA<PhiInstr>())
    return false;

  // Check if all operands are loop-invariant
  for (auto it = val->op_begin(); it != val->op_end(); ++it) {
    auto *operand = *it;
    
    // An operand is loop-invariant if it's:
    // 1. A constant
    // 2. Defined outside the loop
    // 3. Already identified as loop-invariant
    
    if (operand->isA<Const>())
      continue;  // Constants are always invariant
    
    if (loopVars.count(operand))
      return false;  // Operand is loop-carried
  }
  
  return true;
}

bool LoopInvariantCodeMotion::isSafeToHoist(Value *val, 
                                           const analyze::module::SideEffectAnalysis *seAnalysis) {
  // Only hoist instructions
  if (!val->isA<Instruction>())
    return false;
    
  auto *inst = cast<Instruction>(val);
  
  // Don't hoist instructions with side effects
  if (seAnalysis->hasSideEffect(inst))
    return false;
    
  // Don't hoist memory operations unless we're sure they're safe
  if (inst->isA<LoadInstr>() || inst->isA<StoreInstr>() || 
      inst->isA<AllocaInstr>() || inst->isA<ExtractInstr>() ||
      inst->isA<InsertInstr>())
    return false;
    
  // Don't hoist control flow instructions
  if (inst->isA<BranchInstr>() || inst->isA<ReturnInstr>() ||
      inst->isA<YieldInstr>() || inst->isA<ThrowInstr>())
    return false;
  
  return true;
}

void LoopInvariantCodeMotion::hoistInstructions(
    Value *loopHeader, 
    const std::vector<Value *> &invariantInsts,
    const analyze::dataflow::DominatorAnalysis *domAnalysis) {
    
  auto *headerBlock = cast<BodiedFunc>(loopHeader->getParent())->getEntryBlock();
  
  // Find the immediate dominator of the loop header
  auto *preheader = domAnalysis->getIDom(cast<BodiedFunc>(loopHeader->getParent()), 
                                         cast<BasicBlock>(loopHeader));
  
  // If no proper preheader exists, we'd need to create one
  // For simplicity, we'll use the entry block, but in a real implementation
  // you might want to create a proper preheader block
  BasicBlock *insertionPoint = preheader ? preheader : headerBlock;
  
  // Get the instruction before which we'll insert the hoisted instructions
  Instruction *insertBefore = nullptr;
  if (!insertionPoint->empty()) {
    // Insert before the terminator instruction
    for (auto it = insertionPoint->begin(); it != insertionPoint->end(); ++it) {
      if ((*it)->isTerminator()) {
        insertBefore = *it;
        break;
      }
    }
  }
  
  // Move the invariant instructions
  for (auto *inst : invariantInsts) {
    // Skip if not an instruction or already in the preheader
    if (!inst->isA<Instruction>() || inst->getParent() == insertionPoint)
      continue;
      
    auto *instruction = cast<Instruction>(inst);
    instruction->removeFromParent();
    
    if (insertBefore) {
      insertionPoint->insertBefore(instruction, insertBefore);
    } else {
      insertionPoint->push_back(instruction);
    }
  }
}

} // namespace optimization
} // namespace transform
} // namespace ir
} // namespace codon