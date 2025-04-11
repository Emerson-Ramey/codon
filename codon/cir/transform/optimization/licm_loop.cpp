// Copyright (C) 2022-2025 Exaloop Inc. <https://exaloop.io>

#include "licm.h"

#include <queue>
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
  auto *cfgAnalysis = getAnalysisResult<analyze::dataflow::CFAnalysis>(cfgAnalysisKey);
  auto *seAnalysis = getAnalysisResult<analyze::module::SideEffectAnalysis>(seAnalysisKey);
  
  bool changed = false;
  
  // Process each function in the module
  for (auto *func : *module) {
    // Skip functions without a body
    if (!func || !func->isA<BodiedFunc>() || !func->hasValue())
      continue;
      
    auto *bodiedFunc = cast<BodiedFunc>(func);
    
    // Get CFG
    auto *cfg = cfgAnalysis->getCFG(bodiedFunc);
    
    // Find all natural loops in the function
    auto loops = findNaturalLoops(bodiedFunc, cfg, domAnalysis);
    
    // Perform LICM on each loop
    for (auto &loop : loops) {
      if (performLICM(loop, bodiedFunc, domAnalysis, seAnalysis)) {
        changed = true;
      }
    }
  }
  
  // Mark whether we've made changes to the module
  if (changed) {
    markAllAnalysesPreserved();
  }
}

std::vector<LoopInvariantCodeMotion::Loop> LoopInvariantCodeMotion::findNaturalLoops(
    BodiedFunc *func, 
    analyze::dataflow::CFGraph *cfg,
    analyze::dataflow::DominatorAnalysis *domAnalysis) {
  
  std::vector<Loop> loops;
  
  // Get all basic blocks in the function
  std::vector<BasicBlock *> blocks;
  for (auto *block : *func) {
    if (block->isA<BasicBlock>()) {
      blocks.push_back(cast<BasicBlock>(block));
    }
  }
  
  // Find back edges by checking if a successor dominates its predecessor
  for (auto *block : blocks) {
    for (auto it = block->successors_begin(); it != block->successors_end(); ++it) {
      auto *succ = *it;
      
      // Check if successor dominates this block (indicates a back edge)
      if (domAnalysis->dominates(func, succ, block)) {
        // Found a back edge from block to succ, where succ is the loop header
        Loop loop;
        loop.header = cast<BasicBlock>(succ);
        
        // Collect all blocks in the loop
        std::queue<BasicBlock *> workList;
        workList.push(block);
        loop.blocks.insert(cast<BasicBlock>(succ)); // Header is part of the loop
        
        while (!workList.empty()) {
          auto *current = workList.front();
          workList.pop();
          
          if (loop.blocks.insert(current).second) {
            // For each predecessor of the current block
            for (auto predIt = current->predecessors_begin(); 
                 predIt != current->predecessors_end(); 
                 ++predIt) {
              auto *pred = *predIt;
              if (pred->isA<BasicBlock>() && pred != loop.header) {
                workList.push(cast<BasicBlock>(pred));
              }
            }
          }
        }
        
        // Try to identify a preheader
        loop.preheader = nullptr;
        for (auto predIt = loop.header->predecessors_begin(); 
             predIt != loop.header->predecessors_end(); 
             ++predIt) {
          auto *pred = *predIt;
          if (pred->isA<BasicBlock>() && !loop.blocks.count(cast<BasicBlock>(pred))) {
            // Found a predecessor that's not in the loop - it's a preheader candidate
            if (!loop.preheader && pred->getNumSuccessors() == 1) {
              loop.preheader = cast<BasicBlock>(pred);
            } else {
              // Multiple entries to the loop or predecessor has multiple exits
              loop.preheader = nullptr;
              break;
            }
          }
        }
        
        loops.push_back(loop);
      }
    }
  }
  
  return loops;
}

BasicBlock *LoopInvariantCodeMotion::getOrCreatePreheader(Loop &loop, BodiedFunc *func) {
  // If we already have a preheader, return it
  if (loop.preheader)
    return loop.preheader;
  
  // Otherwise, we need to create one
  auto *header = loop.header;
  
  // Create a new basic block to serve as preheader
  auto *preheader = util::makeBasicBlock(func, "loop.preheader");
  
  // Collect all predecessors that are not part of the loop
  std::vector<BasicBlock *> outsidePreds;
  for (auto predIt = header->predecessors_begin(); 
       predIt != header->predecessors_end(); 
       ++predIt) {
    auto *pred = *predIt;
    if (pred->isA<BasicBlock>() && !loop.blocks.count(cast<BasicBlock>(pred))) {
      outsidePreds.push_back(cast<BasicBlock>(pred));
    }
  }
  
  // Redirect all outside predecessors to the preheader
  for (auto *pred : outsidePreds) {
    // Update the terminator of the predecessor
    auto *term = pred->getTerminator();
    for (unsigned i = 0; i < term->getNumSuccessors(); i++) {
      if (term->getSuccessor(i) == header) {
        term->setSuccessor(i, preheader);
      }
    }
  }
  
  // Create a branch from preheader to header
  auto *branch = util::makeBranch(preheader, header);
  preheader->push_back(branch);
  
  // Update phi nodes in the header
  for (auto *phi : header->getPhis()) {
    // Collect incoming values from outside predecessors
    Value *incomingVal = nullptr;
    for (unsigned i = 0; i < phi->getNumIncoming(); i++) {
      auto *incomingBlock = phi->getIncomingBlock(i);
      if (!loop.blocks.count(cast<BasicBlock>(incomingBlock))) {
        incomingVal = phi->getIncomingValue(i);
        break;
      }
    }
    
    if (incomingVal) {
      // Remove old incoming edges from outside predecessors
      for (auto it = outsidePreds.rbegin(); it != outsidePreds.rend(); ++it) {
        phi->removeIncoming(*it);
      }
      
      // Add new incoming edge from preheader
      phi->addIncoming(incomingVal, preheader);
    }
  }
  
  loop.preheader = preheader;
  return preheader;
}

bool LoopInvariantCodeMotion::performLICM(
    Loop &loop, 
    BodiedFunc *func,
    analyze::dataflow::DominatorAnalysis *domAnalysis,
    analyze::module::SideEffectAnalysis *seAnalysis) {
  
  // Ensure we have a preheader
  auto *preheader = getOrCreatePreheader(loop, func);
  if (!preheader)
    return false;
  
  bool changed = false;
  
  // Identify loop-carried variables (via phi nodes)
  std::unordered_set<Value *> loopCarriedVars;
  for (auto *block : loop.blocks) {
    for (auto *phi : block->getPhis()) {
      loopCarriedVars.insert(phi);
      
      // Add operands from within the loop
      for (unsigned i = 0; i < phi->getNumIncoming(); i++) {
        auto *incomingBlock = phi->getIncomingBlock(i);
        if (loop.blocks.count(cast<BasicBlock>(incomingBlock))) {
          loopCarriedVars.insert(phi->getIncomingValue(i));
        }
      }
    }
  }
  
  // First pass: identify load instructions that could benefit from LICM
  std::vector<LoadInstr *> invariantLoads;
  std::unordered_map<LoadInstr *, StoreInstr *> loadToStoreMap;
  
  for (auto *block : loop.blocks) {
    for (auto *inst : *block) {
      if (auto *load = dyn_cast<LoadInstr>(inst)) {
        Value *loadAddress = load->getPtr();
        bool hasStoreInLoop = false;
        StoreInstr *storeOutsideLoop = nullptr;
        
        // Check if there are any stores to this address in the loop
        for (auto *loopBlock : loop.blocks) {
          for (auto *loopInst : *loopBlock) {
            if (auto *store = dyn_cast<StoreInstr>(loopInst)) {
              if (store->getPtr() == loadAddress) {
                hasStoreInLoop = true;
                break;
              }
            }
          }
          if (hasStoreInLoop)
            break;
        }
        
        // If no stores in the loop, check for stores outside
        if (!hasStoreInLoop) {
          // Ideally we'd check for stores in blocks not in the loop that might
          // update the loaded value, but this is simplified
          invariantLoads.push_back(load);
        }
      }
    }
  }
  
  // Hoist invariant loads
  for (auto *load : invariantLoads) {
    // Create an alloca in the preheader
    auto *allocaInstr = util::makeAlloca(preheader, load->getType(), "licm.load");
    allocaInstr->setAlign(load->getAlign());
    
    // Clone the load and insert it into the preheader
    auto *clonedLoad = cast<LoadInstr>(util::clone(load));
    preheader->insertBefore(clonedLoad, preheader->getTerminator());
    
    // Store the loaded value into the stack-allocated space
    auto *storeInstr = util::makeStore(preheader, clonedLoad, allocaInstr);
    preheader->insertBefore(storeInstr, preheader->getTerminator());
    
    // Update the original load to load from the stack space
    auto *inplaceLoad = util::makeLoad(load->getParent(), allocaInstr, load->getType());
    inplaceLoad->setAlign(load->getAlign());
    load->getParent()->insertBefore(inplaceLoad, load);
    
    // Replace all uses of the original load with the new load
    load->replaceAllUsesWith(inplaceLoad);
    
    // Erase the original load if it has no uses
    if (load->getNumUses() == 0) {
      load->eraseFromParent();
    }
    
    changed = true;
  }
  
  // Second pass: identify other invariant instructions
  std::vector<Instruction *> invariantInsts;
  for (auto *block : loop.blocks) {
    for (auto *inst : *block) {
      if (isLoopInvariant(inst, loopCarriedVars) && isSafeToHoist(inst, seAnalysis)) {
        invariantInsts.push_back(cast<Instruction>(inst));
      }
    }
  }
  
  // Sort instructions by their dependencies
  std::sort(invariantInsts.begin(), invariantInsts.end(), 
    [](Instruction *a, Instruction *b) {
      // Simple ordering - if b uses a, a should come first
      for (auto it = b->op_begin(); it != b->op_end(); ++it) {
        if (*it == a)
          return true;
      }
      return false;
    });
  
  // Hoist other invariant instructions
  for (auto *inst : invariantInsts) {
    inst->removeFromParent();
    preheader->insertBefore(inst, preheader->getTerminator());
    changed = true;
  }
  
  return changed;
}

bool LoopInvariantCodeMotion::isLoopInvariant(Value *val, const std::unordered_set<Value *> &loopVars) {
  // Skip non-instructions or special cases
  if (!val || !val->isA<Instruction>() || val->isA<PhiInstr>())
    return false;

  // Check if all operands are loop-invariant
  for (auto it = val->op_begin(); it != val->op_end(); ++it) {
    auto *operand = *it;
    
    // An operand is loop-invariant if it's:
    // 1. A constant
    // 2. Not in the loop-carried variables set
    
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
  
  // Be extra cautious with memory operations except for loads that were specifically identified
  if (inst->isA<StoreInstr>() || inst->isA<AllocaInstr>() || 
      inst->isA<ExtractInstr>() || inst->isA<InsertInstr>())
    return false;
    
  // Don't hoist control flow instructions
  if (inst->isA<BranchInstr>() || inst->isA<ReturnInstr>() ||
      inst->isA<YieldInstr>() || inst->isA<ThrowInstr>())
    return false;
  
  return true;
}

} // namespace optimization
} // namespace transform
} // namespace ir
} // namespace codon