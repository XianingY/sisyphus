#include "Options.h"
#include "PipelineProfiles.h"
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstring>

#include "../frontend/FrontendFacade.h"
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

  sys::CodeGen cg(node);
  delete node;

  sys::ModuleOp *module = cg.getModule();
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
