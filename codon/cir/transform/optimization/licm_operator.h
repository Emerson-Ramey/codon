#pragma once

#include <unordered_set>
#include <vector>

#include "codon/cir/transform/pass.h"
#include "codon/cir/analyze/module/side_effect.h"
#include "codon/cir/analyze/dataflow/dominator.h"

namespace codon {
namespace ir {
namespace transform {
namespace optimizations {

/// Loop-Invariant Code Motion pass that identifies expressions in loops
/// that don't change across iterations and hoists them to the loop preheader.
class LICMPass : public OperatorPass {
    private:
        std::string dominatorKey;
        std::string sideEffectsKey;
        std::string reachingDefsKey;

    public:
        /// Constructs a licm pass.
        /// @param dominatorKey the dominator analysis key
        /// @param sideEffectsKey the dominator analysis key
        /// @param reachingDefsKey the reaching definitions analysis key
        LICMPass(const std::string &dominatorKey, const std::string &sideEffectsKey, const std::string &reachingDefsKey, const std::string &cfAnalysisKey)
            : OperatorPass(/*childrenFirst=*/true), dominatorKey(dominatorKey), sideEffectsKey(sideEffectsKey), reachingDefsKey(reachingDefsKey) {} // children first = true is good for processing nested loops before their parent loops

        static const std::string KEY;
        std::string getKey() const override { return KEY; }

        void run(Module *m) override;
        void handle(ForFlow *v) override;
        void handle(ImperativeForFlow *v) override;
        void handle(WhileFlow *v) override;
    
    private:
        /// Performs the actual code motion for a loop
        /// @param loop the loop to optimize
        /// @param body the loop's body
        /// @param parent the loop's parent
        template <typename T>
        void performCodeMotion(T *loop, SeriesFlow *body, SeriesFlow *parent);

        /// Analyzes whether an expression is loop-invariant
        /// @param expr the expression to check
        /// @param loop the loop containing the expression
        /// @return true if the expression is loop-invariant
        template <typename T>
        bool isLoopInvariant(Value *expr, T *loop);

        /// Collects variables modified within a loop
        /// @param loop the loop to analyze
        /// @return set of variables modified in the loop
        template <typename T>
        std::unordered_set<Var *> collectModifiedVars(T *loop);
        
        /// Identifies variables used in an expression
        /// @param expr the expression to analyze
        /// @return vector of used variables
        std::vector<Var *> getUsedVars(Value *expr);       
    };

} // namespace optimizations
} // namespace transform
} // namespace ir
} // namespace codon