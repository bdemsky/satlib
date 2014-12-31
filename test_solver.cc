#include "inc_solver.h"

int main(int argc, char **argv) {
  IncrementalSolver * s=new IncrementalSolver();
  s->addClauseLiteral(1);s->addClauseLiteral(2);s->addClauseLiteral(0);
  s->finishedClauses();
  s->freeze(1); s->freeze(2);
  printf("solution=%d\n", s->solve());
  s->addClauseLiteral(-1);s->addClauseLiteral(0);
  s->finishedClauses();
  printf("solution=%d\n", s->solve());
  s->addClauseLiteral(-2);s->addClauseLiteral(0);
  s->finishedClauses();
  printf("solution=%d\n", s->solve());
  delete s;
}
