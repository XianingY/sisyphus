#include "Options.h"
#include "PipelineProfiles.h"
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <iostream>
#include <sstream>
#include <unordered_set>
#include <vector>

#include "../frontend/FrontendFacade.h"
#include "../cfg/CFGOps.h"
#include "../cfg/HIRToCFG.h"
#include "../cfg/CFGVerifier.h"
#include "../cfg/CFGLegality.h"
#include "../cfg/CFGToLegacy.h"
#include "../hir/HIRBuilder.h"
#include "../hir/HIRVerifier.h"
#include "../hir/HIRCanonicalize.h"
#include "../pass/PassRegistry.h"
#include "../utils/smt/SMT.h"

using namespace smt;

sys::Options opts;

void removeDuplicates(std::vector<Atomic>& clause) {
  std::sort(clause.begin(), clause.end());
  auto last = std::unique(clause.begin(), clause.end());
  clause.erase(last, clause.end());
}

void sat() {
  Solver solver;
  std::string line;
  std::getline(std::cin, line);
  while (line[0] == 'c')
    std::getline(std::cin, line);

  std::istringstream headerStream(line);
  int n, m;
  std::string dummy;
  headerStream >> dummy >> dummy >> m >> n;
  solver.init(m);

  for (int i = 0; i < n; ++i) {
    std::getline(std::cin, line);

    std::vector<Atomic> clause;
    std::istringstream lineStream(line);
    int lit;

    while (lineStream >> lit) {
      if (lit == 0)
        break;
      auto var = std::abs(lit) - 1;
      clause.push_back((Atomic) (lit < 0 ? var * 2 + 1 : var * 2));
    }

    removeDuplicates(clause);
    solver.addClause(clause);
  }
  std::vector<signed char> assignments;
  bool succ = solver.solve(assignments);
  if (!succ) {
    std::cout << "unsat\n";
    return;
  }

  std::cout << "sat\n";
  for (int i = 0; i < m; i++)
    std::cout << (i + 1) << " = " << (assignments[i] ? "true" : "false") << "\n";
}

void bv(const sys::Options &opts) {
  const auto &infer = [&](BvSolver &solver, BvExpr *x) {
    bool succ = solver.infer(x);
    if (succ) {
      std::cout << "sat\n";
      std::cout << "x = " << solver.extract("x") << "\n";
    } else std::cout << "unsat\n";
  };

  BvExprContext ctx;
  assert(ctx.create(BvExpr::Var, "x") == ctx.create(BvExpr::Var, "x"));

  // Test: x = (x == 1) ? 2x : x + 1
  // > unsat 
  if (true) {
    BvSolver solver(opts);
    BvExprContext ctx;

    auto _1 = ctx.create(BvExpr::Const, 1);
    auto _2 = ctx.create(BvExpr::Const, 2);
    auto _3 = ctx.create(BvExpr::Var, "x");
    auto _4 = ctx.create(BvExpr::Eq, _3, _1);
    auto _5 = ctx.create(BvExpr::Add, _3, _1);
    auto _6 = ctx.create(BvExpr::Mul, _3, _2);
    auto _7 = ctx.create(BvExpr::Ite, _4, _6, _5);
    auto _8 = ctx.create(BvExpr::Eq, _3, _7);

    infer(solver, _8);
  }

  // Test: 1089 * 2256 = 74448 * (x - 16)
  // > sat, x = 1879048241 (signed wrap)
  // (Note that x = 49 is the obvious solution.)
  if (true) {
    BvSolver solver(opts);
    BvExprContext ctx;

    auto _1 = ctx.create(BvExpr::Var, "x");
    auto _2 = ctx.create(BvExpr::Const, 16);
    auto _3 = ctx.create(BvExpr::Const, 1089);
    auto _4 = ctx.create(BvExpr::Const, 2256);
    auto _5 = ctx.create(BvExpr::Const, 74448);
    auto _6 = ctx.create(BvExpr::Mul, _3, _4);
    auto _7 = ctx.create(BvExpr::Sub, _1, _2);
    auto _8 = ctx.create(BvExpr::Mul, _7, _5);
    auto _9 = ctx.create(BvExpr::Eq, _6, _8);

    infer(solver, _9);
  }
  
  // Test: 7 / x = -2
  // > sat, x = -3
  // Note: very expensive, ~0.2s
  if (true) {
    BvSolver solver(opts);
    BvExprContext ctx;

    auto _1 = ctx.create(BvExpr::Const, 7);
    auto _2 = ctx.create(BvExpr::Var, "x");
    auto _3 = ctx.create(BvExpr::Div, _1, _2);
    auto _4 = ctx.create(BvExpr::Const, -2);
    auto _5 = ctx.create(BvExpr::Eq, _4, _3);

    infer(solver, _5);
  }

  // Test: -9 % 2 == -1
  if (true) {
    BvSolver solver(opts);
    BvExprContext ctx;

    auto _1 = ctx.create(BvExpr::Const, -9);
    auto _2 = ctx.create(BvExpr::Const, 2);
    auto _3 = ctx.create(BvExpr::Var, "x");
    auto _4 = ctx.create(BvExpr::Mod, _1, _2);
    auto _5 = ctx.create(BvExpr::Eq, _3, _4);
    // _5 = simplify(_5, ctx);

    infer(solver, _5);
  }
}

int main(int argc, char **argv) {
  opts = sys::parseArgs(argc, argv);

  // Test for submodules: bitvector SMT solver, and CDCL SAT solver.
  if (opts.bv) {
    bv(opts);
    return 0;
  }
  if (opts.sat) {
    sat();
    return 0;
  }

  // Read input file.
  std::ifstream ifs(opts.inputFile);
  if (!ifs) {
    std::cerr << "cannot open file\n";
    return 1;
  }

  std::stringstream ss;
  // Add a newline at the end.
  // Single-line comments cannot terminate with EOF.
  ss << ifs.rdbuf() << "\n";

  sys::TypeContext ctx;

  sys::Parser parser(ss.str(), ctx);
  sys::ASTNode *node = parser.parse();
  sys::Sema sema(node, ctx);

  std::unique_ptr<sys::CodeGen> cg;
  std::unique_ptr<sys::ModuleOp> loweredModule;
  std::unique_ptr<sys::hir::Module> hirModule;
  std::unique_ptr<sys::cfg::Module> cfgModule;

  auto runStage = [&](const char *stageName, auto &&fn) {
    auto start = std::chrono::steady_clock::now();
    fn();
    if (opts.dumpPassTiming) {
      auto end = std::chrono::steady_clock::now();
      double ms = std::chrono::duration<double, std::milli>(end - start).count();
      std::cerr << "[stage-timing] " << stageName << " : " << ms << " ms\n";
    }
  };
  auto stageDumpPath = [&](const std::string &stage, const std::string &payload) {
    auto path = "/tmp/sisyphus-" + stage + ".dump";
    std::ofstream ofs(path);
    ofs << payload;
    return path;
  };
  auto failStage = [&](const std::string &stage, const std::vector<std::string> &errors, const std::string &payload) {
    auto dumpPath = stageDumpPath(stage, payload);
    std::cerr << "[stage-fail] file=" << opts.inputFile
              << " stage=" << stage
              << " dump=" << dumpPath << "\n";
    for (const auto &e : errors)
      std::cerr << "  - " << e << "\n";
    std::exit(1);
  };

  bool dumpHIR = opts.dumpHIR;
  if (const char *env = std::getenv("SISY_DUMP_HIR"))
    dumpHIR = dumpHIR || (env[0] && std::strcmp(env, "0") != 0);
  bool dumpCFG = opts.dumpCFG;
  if (const char *env = std::getenv("SISY_DUMP_CFG"))
    dumpCFG = dumpCFG || (env[0] && std::strcmp(env, "0") != 0);
  auto requiresLegacyFallback = [&](const sys::hir::Module &mod, std::vector<std::string> &reasons) {
    reasons.clear();
    std::unordered_set<std::string> seen;
    std::vector<const sys::hir::Op*> stack;
    if (mod.root)
      stack.push_back(mod.root.get());

    while (!stack.empty()) {
      auto *op = stack.back();
      stack.pop_back();
      if (!op)
        continue;

      if (op->kind == sys::hir::OpKind::VarDecl) {
        if (auto *var = sys::dyn_cast<sys::VarDeclNode>(op->origin)) {
          if (var->global && seen.insert("global-vardecl").second)
            reasons.push_back("global variable declaration");
          if (var->init && (sys::isa<sys::ConstArrayNode>(var->init) || sys::isa<sys::LocalArrayNode>(var->init)) &&
              seen.insert("array-init").second)
            reasons.push_back("array initializer");
        }
        if (!op->arrayDims.empty() && seen.insert("array-decl").second)
          reasons.push_back("array declaration");
      }

      if ((op->kind == sys::hir::OpKind::Load || op->kind == sys::hir::OpKind::Store) &&
          (op->type == sys::hir::TypeKind::Array || op->type == sys::hir::TypeKind::Pointer) &&
          seen.insert("ptr-array-memory").second)
        reasons.push_back("pointer/array memory access");

      for (const auto &child : op->children)
        if (child)
          stack.push_back(child.get());
    }
    return !reasons.empty();
  };

  if (opts.useLegacyCodegen) {
    cg = std::make_unique<sys::CodeGen>(node);
  } else {
    runStage("hir.build", [&]() {
      sys::hir::Builder builder;
      hirModule = std::make_unique<sys::hir::Module>(builder.build(node));
    });
    if (dumpHIR) {
      std::cerr << "===== HIR =====\n";
      sys::hir::dump(*hirModule, std::cerr);
    }

    if (opts.verifyHIR) {
      runStage("hir.verify.pre", [&]() {
        std::vector<std::string> errors;
        if (!sys::hir::verify(*hirModule, errors)) {
          std::ostringstream os;
          sys::hir::dump(*hirModule, os);
          failStage("hir-verify-pre", errors, os.str());
        }
      });
    }

    runStage("hir.canonicalize", [&]() {
      sys::hir::Canonicalizer canonicalizer;
      auto stats = canonicalizer.run(*hirModule);
      if (opts.verbose || opts.stats) {
        std::cerr << "[hir] const_folded=" << stats.constFolded
                  << " dead_branches=" << stats.deadBranchesEliminated << "\n";
      }
    });

    if (opts.verifyHIR) {
      runStage("hir.verify.post", [&]() {
        std::vector<std::string> errors;
        if (!sys::hir::verify(*hirModule, errors)) {
          std::ostringstream os;
          sys::hir::dump(*hirModule, os);
          failStage("hir-verify-post", errors, os.str());
        }
      });
    }

    std::vector<std::string> fallbackReasons;
    if (requiresLegacyFallback(*hirModule, fallbackReasons)) {
      if (opts.verbose || opts.stats) {
        std::cerr << "[dialect-fallback] switch to legacy codegen due to:\n";
        for (const auto &r : fallbackReasons)
          std::cerr << "  - " << r << "\n";
      }
      cg = std::make_unique<sys::CodeGen>(node);
    }

    if (!cg) {
      runStage("hir.to-cfg", [&]() {
        std::vector<std::string> errors;
        cfgModule = std::make_unique<sys::cfg::Module>(sys::cfg::lowerFromHIR(*hirModule, errors));
        if (!errors.empty()) {
          std::ostringstream os;
          if (cfgModule)
            sys::cfg::dump(*cfgModule, os);
          failStage("hir-to-cfg", errors, os.str());
        }
      });
      if (dumpCFG) {
        std::cerr << "===== CFG =====\n";
        sys::cfg::dump(*cfgModule, std::cerr);
      }

      if (opts.verifyCFG) {
        runStage("cfg.legality", [&]() {
          std::vector<std::string> errors;
          if (!sys::cfg::verifyHIRToCFGConversion(*hirModule, *cfgModule, errors)) {
            std::ostringstream os;
            sys::cfg::dump(*cfgModule, os);
            failStage("cfg-legality", errors, os.str());
          }
        });
        runStage("cfg.verify", [&]() {
          std::vector<std::string> errors;
          if (!sys::cfg::verify(*cfgModule, errors)) {
            std::ostringstream os;
            sys::cfg::dump(*cfgModule, os);
            failStage("cfg-verify", errors, os.str());
          }
        });
      }

      runStage("cfg.to-legacy", [&]() {
        std::vector<std::string> errors;
        loweredModule = sys::cfg::lowerToLegacyIR(*cfgModule, errors);
        if (!loweredModule || !errors.empty()) {
          std::ostringstream os;
          if (cfgModule)
            sys::cfg::dump(*cfgModule, os);
          failStage("cfg-to-legacy", errors, os.str());
        }
      });
    }
  }
  delete node;

  sys::ModuleOp *module = cg ? cg->getModule() : loweredModule.get();
  if (opts.dumpMidIR) {
    if (opts.emitIR)
      std::cerr << "===== Initial IR =====\n";
    std::cerr << module;
  }

  sys::PassManager pm(module, opts);
  auto plan = sys::pipeline::configurePipeline(pm, opts);
  if (const char *env = std::getenv("SISY_DUMP_PIPELINE_PROFILE")) {
    if (env[0] && std::strcmp(env, "0") != 0) {
      std::cerr << "[pipeline] " << sys::pipeline::formatPlan(plan) << "\n";
      pm.dumpPipelineProfile(std::cerr);
    }
  }
  
  pm.run();
  return 0;
}
