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
  auto *cfgAnalysis = getAnalysisResult<analyze::dataflow::CFAnalysis>(cfgAnalysisKey);
  
  bool changed = false;
  
  // Process each function in the module
  for (auto *func : *module) {
    // Skip functions without a body
    if (!func || !func->isA<BodiedFunc>() || !func->hasValue())
      continue;
      
    auto *bodiedFunc = cast<BodiedFunc>(func);
    
    // Find all natural loops in the function
    auto *cfg = cfgAnalysis->getCFG(bodiedFunc);
    auto loops = cfg->findNaturalLoops();
    
    // Process each loop
    for (auto &loopPair : loops) {
      auto *header = loopPair.first;
      auto &bodyBlocks = loopPair.second;
      
      // Get the loop preheader
      auto *preheader = cfg->getLoopPreheader(header);
      if (!preheader)
        continue;
      
      // Similar to the LLVM code, find the frequent and infrequent paths
      // For simplicity, we'll consider all blocks in the loop as the frequent path
      // In a real implementation, you'd use profiling data or heuristics
      std::unordered_set<Value *> frequentPath;
      std::unordered_set<Value *> infrequentPath;
      
      // For now, let's consider all blocks as part of the frequent path
      // In a real implementation, you'd analyze branch probabilities
      for (auto *bb : bodyBlocks) {
        frequentPath.insert(bb);
      }
      
      // Identify loads that are invariant on the frequent path and their corresponding stores
      std::vector<LoadInstr *> invariantLoads;
      std::unordered_map<LoadInstr *, StoreInstr *> loadToStoreMap;
      
      for (auto *bb : frequentPath) {
        if (!bb->isA<BasicBlock>())
          continue;
          
        auto *basicBlock = cast<BasicBlock>(bb);
        
        for (auto *inst : *basicBlock) {
          // Check if the instruction is a load
          if (auto *load = dyn_cast<LoadInstr>(inst)) {
            Value *loadAddress = load->getPtr();
            bool hasStoreInFrequent = false;
            StoreInstr *infrequentStore = nullptr;
            
            // Check if there is a store to the same address on the frequent path
            for (auto *freqBB : frequentPath) {
              if (!freqBB->isA<BasicBlock>())
                continue;
                
              auto *freqBasicBlock = cast<BasicBlock>(freqBB);
              
              for (auto *freqInst : *freqBasicBlock) {
                if (auto *store = dyn_cast<StoreInstr>(freqInst)) {
                  if (store->getPtr() == loadAddress) {
                    hasStoreInFrequent = true;
                    break;
                  }
                }
              }
              
              if (hasStoreInFrequent)
                break;
            }
            
            // Check if there is a store on the infrequent path
            if (!hasStoreInFrequent) {
              for (auto *infreqBB : infrequentPath) {
                if (!infreqBB->isA<BasicBlock>())
                  continue;
                  
                auto *infreqBasicBlock = cast<BasicBlock>(infreqBB);
                
                for (auto *infreqInst : *infreqBasicBlock) {
                  if (auto *store = dyn_cast<StoreInstr>(infreqInst)) {
                    if (store->getPtr() == loadAddress) {
                      infrequentStore = store;
                      break;
                    }
                  }
                }
                
                if (infrequentStore)
                  break;
              }
            }
            
            // If the load satisfies both conditions, add it to our list
            if (!hasStoreInFrequent && infrequentStore) {
              invariantLoads.push_back(load);
              loadToStoreMap[load] = infrequentStore;
            }
          }
        }
      }
      
      // Perform LICM on the invariant loads
      for (auto *load : invariantLoads) {
        // Create an alloca in the preheader
        auto *allocaInstr = util::makeAlloca(preheader, load->getType(), "moved_load_alloca");
        allocaInstr->setAlign(load->getAlign());
        
        // Clone the load and insert it into the preheader
        auto *clonedLoad = cast<LoadInstr>(util::clone(load));
        preheader->insertBefore(clonedLoad, preheader->getTerminator());
        
        // Store the loaded value into the stack-allocated space
        auto *storeInstr = util::makeStore(preheader, clonedLoad, allocaInstr);
        preheader->insertBefore(storeInstr, preheader->getTerminator());
        
        // Update the store in the infrequent path
        if (auto *infrequentStore = loadToStoreMap[load]) {
          infrequentStore->setPtr(allocaInstr);
          
          // Clone the store for cases with multiple loads hoisted for one store
          auto *clonedStore = cast<StoreInstr>(util::clone(infrequentStore));
          infrequentStore->getParent()->insertAfter(clonedStore, infrequentStore);
        }
        
        // Modify the original load to load from the stack space
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
      
      // Now perform traditional LICM on other instructions
      hoistInvariantInstructions(bodiedFunc, header, bodyBlocks, preheader, domAnalysis, seAnalysis);
    }
  }
  
  // Mark whether we've made changes to the module
  if (changed) {
    markAllAnalysesPreserved();
  }
}

void LoopInvariantCodeMotion::hoistInvariantInstructions(
    BodiedFunc *func,
    Value *loopHeader,
    const std::unordered_set<Value *> &loopBlocks,
    BasicBlock *preheader,
    const analyze::dataflow::DominatorAnalysis *domAnalysis,
    const analyze::module::SideEffectAnalysis *seAnalysis) {
  
  // Identify loop-carried dependencies
  std::unordered_set<Value *> loopCarriedVars;
  for (auto *block : loopBlocks) {
    if (!block->isA<BasicBlock>())
      continue;
    
    auto *basicBlock = cast<BasicBlock>(block);
    
    for (auto *phi : basicBlock->getPhis()) {
      loopCarriedVars.insert(phi);
      
      // Add phi operands coming from within the loop
      for (auto it = phi->op_begin(); it != phi->op_end(); ++it) {
        auto *val = *it;
        if (val->getParent() && loopBlocks.count(val->getParent())) {
          loopCarriedVars.insert(val);
        }
      }
    }
  }
  
  // Collect loop instructions
  std::vector<Value *> loopInstructions;
  for (auto *block : loopBlocks) {
    if (!block->isA<BasicBlock>())
      continue;
    
    auto *basicBlock = cast<BasicBlock>(block);
    
    for (auto *inst : *basicBlock) {
      loopInstructions.push_back(inst);
    }
  }
  
  // Find invariant instructions
  std::vector<Value *> invariantInsts;
  for (auto *inst : loopInstructions) {
    if (isLoopInvariant(inst, loopCarriedVars) && isSafeToHoist(inst, seAnalysis)) {
      invariantInsts.push_back(inst);
    }
  }
  
  // Sort invariant instructions to maintain dependencies
  std::sort(invariantInsts.begin(), invariantInsts.end(), 
    [&loopInstructions](Value *a, Value *b) {
      // Find positions in the original instruction stream
      auto posA = std::find(loopInstructions.begin(), loopInstructions.end(), a);
      auto posB = std::find(loopInstructions.begin(), loopInstructions.end(), b);
      return posA < posB;
    });
  
  // Hoist the instructions
  for (auto *inst : invariantInsts) {
    if (!inst->isA<Instruction>())
      continue;
    
    auto *instruction = cast<Instruction>(inst);
    instruction->removeFromParent();
    
    // Insert before the terminator
    preheader->insertBefore(instruction, preheader->getTerminator());
  }
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
  
  // Be extra cautious with memory operations
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

} // namespace optimization
} // namespace transform
} // namespace ir
} // namespace codon