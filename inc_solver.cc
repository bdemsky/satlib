#include "inc_solver.h"

#define SATSOLVER "sat_solver"

IncrementalSolver::IncrementalSolver() :
  buffer((int *)malloc(sizeof(int)*BUFFERSIZE)),
  solution(NULL),
  solutionsize(0),
  offset(0)
{
  createSolver();
}

IncrementalSolver::~IncrementalSolver() {
  killSolver();
  free(buffer);
}

void IncrementalSolver::reset() {
  killSolver();
  offset = 0;
  createSolver();
}

void IncrementalSolver::addClauseLiteral(int literal) {
  buffer[offset++]=literal;
  if (offset==BUFFERSIZE) {
    flushBuffer();
  }
}

void IncrementalSolver::finishedClauses() {
  addClauseLiteral(0);
}

void IncrementalSolver::freeze(int variable) {
  addClauseLiteral(IS_FREEZE);
  addClauseLiteral(variable);
}

int IncrementalSolver::solve() {
  //add an empty clause
  addClauseLiteral(IS_RUNSOLVER);
  flushBuffer();
  int result=readIntSolver();
  if (result == IS_SAT) {
    int numVars=readIntSolver();
    if (numVars > solutionsize) {
      if (solution != NULL)
        free(solution);
      solution = (int *) malloc(numVars*sizeof(int));
    }
    readSolver(solution, numVars * sizeof(int));
  }
  return result;
}

int IncrementalSolver::readIntSolver() {
  int value;
  readSolver(&value, 4);
  return value;
}

void IncrementalSolver::readSolver(void * tmp, ssize_t size) {
  char *result = (char *) tmp;
  ssize_t bytestoread=size;
  ssize_t bytesread=0;
  do {
    ssize_t n=read(from_solver_fd, &((char *)result)[bytesread], bytestoread);
    if (n == -1) {
      fprintf(stderr, "Read failure\n");
      exit(-1);
    }
    bytestoread -= n;
    bytesread += n;
  } while(bytestoread != 0);
}

bool IncrementalSolver::getValue(int variable) {
  return solution[variable];
}

void IncrementalSolver::createSolver() {
  int to_pipe[2];
  int from_pipe[2];
  if (pipe(to_pipe) || pipe(from_pipe)) {
    fprintf(stderr, "Error creating pipe.\n");
    exit(-1);
  }
  if ((solver_pid = fork()) == -1) {
    fprintf(stderr, "Error forking.\n");
    exit(-1);
  }
  if (solver_pid == 0) {
    //Solver process
    close(to_pipe[0]);
    close(from_pipe[1]);
    if ((dup2(0, to_pipe[1]) == -1) ||
        (dup2(1, from_pipe[0]) == -1)) {
      fprintf(stderr, "Error duplicating pipes\n");
    }
    setsid();
    execlp(SATSOLVER, SATSOLVER, NULL);
    fprintf(stderr, "execlp Failed\n");
  } else {
    //Our process
    to_solver_fd = to_pipe[0];
    from_solver_fd = from_pipe[1];
    close(to_pipe[1]);
    close(from_pipe[0]);
  }
}

void IncrementalSolver::killSolver() {
  close(to_solver_fd);
  close(from_solver_fd);
  //Stop the solver
  if (solver_pid > 0)
    killpg(solver_pid, SIGKILL);
}

void IncrementalSolver::flushBuffer() {
  ssize_t bytestowrite=sizeof(int)*offset;
  ssize_t byteswritten=0;
  do {
    ssize_t n=write(to_solver_fd, &((char *)buffer)[byteswritten], bytestowrite);
    if (n == -1) {
      fprintf(stderr, "Write failure\n");
      exit(-1);
    }
    bytestowrite -= n;
    byteswritten += n;
  } while(bytestowrite != 0);
  offset = 0;
}
