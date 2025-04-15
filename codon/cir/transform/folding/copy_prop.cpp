// Copyright (C) 2022-2025 Exaloop Inc. <https://exaloop.io>

#include "copy_prop.h"

#include "codon/cir/analyze/dataflow/reaching.h"
#include "codon/cir/analyze/module/global_vars.h"
#include "codon/cir/util/cloning.h"

namespace codon {
namespace ir {
namespace transform {
namespace folding {
namespace {
bool okReg(Value *v) {
  return v && isA<VarValue>(v);
}
} // namespace

const std::string CopyPropPass::KEY = "core-folding-copy-prop";

void CopyPropPass::handle(VarValue *v) {
  auto *M = v->getModule();

  auto *var = v->getVar();

  Value *replacement;
  if (var->isGlobal()) {
    auto *r = getAnalysisResult<analyze::module::GlobalVarsResult>(globalVarsKey);
    if (!r)
      return;

    auto it = r->assignments.find(var->getId());
    if (it == r->assignments.end())
      return;

    auto *copyDef = M->getValue(it->second);
    if (!okReg(copyDef))
      return;

    util::CloneVisitor cv(M);
    replacement = cv.clone(copyDef);
  } else {
    auto *r = getAnalysisResult<analyze::dataflow::RDResult>(reachingDefKey);
    if (!r)
      return;
    auto *c = r->cfgResult;

    auto it = r->results.find(getParentFunc()->getId());
    auto it2 = c->graphs.find(getParentFunc()->getId());
    if (it == r->results.end() || it2 == c->graphs.end())
      return;

    auto *rd = it->second.get();
    auto *cfg = it2->second.get();

    auto reaching = rd->getReachingDefinitions(var, v);

    if (reaching.size() != 1)
      return;

    auto def = *reaching.begin();
    if (def == -1)
      return;

    auto *copyDef = cfg->getValue(def);
    if (!okReg(copyDef))
      return;

    util::CloneVisitor cv(M);
    replacement = cv.clone(copyDef);
  }

  v->replaceAll(replacement);
}

} // namespace folding
} // namespace transform
} // namespace ir
} // namespace codon
