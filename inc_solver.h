#ifndef INC_SOLVER_H
#define INC_SOLVER_H
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include "solver_interface.h"

class IncrementalSolver {
 public:
  IncrementalSolver();
  ~IncrementalSolver();
  void addClauseLiteral(int literal);
  void finishedClauses();
  void freeze(int variable);
  int solve();
  bool getValue(int variable);
  void reset();

 private:
  void createSolver();
  void killSolver();
  void flushBuffer();
  int readIntSolver();
  void readSolver(void * buffer, ssize_t size);
  int * buffer;
  int * solution;
  int solutionsize;
  int offset;
  pid_t solver_pid;
  int to_solver_fd;
  int from_solver_fd;
};
#endif
