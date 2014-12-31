
#include "solver_interface.h"
#include <errno.h>

#include <signal.h>
#include <zlib.h>
#include <sys/resource.h>

#include "utils/System.h"
#include "utils/ParseUtils.h"
#include "utils/Options.h"
#include "core/Dimacs.h"
#include "simp/SimpSolver.h"

using namespace Glucose;


static const char* _certified = "CORE -- CERTIFIED UNSAT";

void printStats(Solver& solver)
{
    double cpu_time = cpuTime();
    double mem_used = 0;//memUsedPeak();
    printf("c restarts              : %" PRIu64" (%" PRIu64" conflicts in avg)\n", solver.starts,(solver.starts>0 ?solver.conflicts/solver.starts : 0));
    printf("c blocked restarts      : %" PRIu64" (multiple: %" PRIu64") \n", solver.nbstopsrestarts,solver.nbstopsrestartssame);
    printf("c last block at restart : %" PRIu64"\n",solver.lastblockatrestart);
    printf("c nb ReduceDB           : %" PRIu64"\n", solver.nbReduceDB);
    printf("c nb removed Clauses    : %" PRIu64"\n",solver.nbRemovedClauses);
    printf("c nb learnts DL2        : %" PRIu64"\n", solver.nbDL2);
    printf("c nb learnts size 2     : %" PRIu64"\n", solver.nbBin);
    printf("c nb learnts size 1     : %" PRIu64"\n", solver.nbUn);

    printf("c conflicts             : %-12" PRIu64"   (%.0f /sec)\n", solver.conflicts   , solver.conflicts   /cpu_time);
    printf("c decisions             : %-12" PRIu64"   (%4.2f %% random) (%.0f /sec)\n", solver.decisions, (float)solver.rnd_decisions*100 / (float)solver.decisions, solver.decisions   /cpu_time);
    printf("c propagations          : %-12" PRIu64"   (%.0f /sec)\n", solver.propagations, solver.propagations/cpu_time);
    printf("c conflict literals     : %-12" PRIu64"   (%4.2f %% deleted)\n", solver.tot_literals, (solver.max_literals - solver.tot_literals)*100 / (double)solver.max_literals);
    printf("c nb reduced Clauses    : %" PRIu64"\n",solver.nbReducedClauses);
    
    if (mem_used != 0) printf("Memory used           : %.2f MB\n", mem_used);
    printf("c CPU time              : %g s\n", cpu_time);
}



static Solver* solver;
// Terminate by notifying the solver and back out gracefully. This is mainly to have a test-case
// for this feature of the Solver as it may take longer than an immediate call to '_exit()'.
static void SIGINT_interrupt(int signum) { solver->interrupt(); }

// Note that '_exit()' rather than 'exit()' has to be used. The reason is that 'exit()' calls
// destructors and may cause deadlocks if a malloc/free function happens to be running (these
// functions are guarded by locks for multithreaded use).
static void SIGINT_exit(int signum) {
    printf("\n"); printf("*** INTERRUPTED ***\n");
    if (solver->verbosity > 0){
        printStats(*solver);
        printf("\n"); printf("*** INTERRUPTED ***\n"); }
    _exit(1); }


int *buffer;
int length;
int offset;

int *outbuffer;
int outoffset;

int getInt() {
  if (offset>=length) {
    ssize_t ptr;
    offset = 0;
    do {
      ptr=read(0, buffer, sizeof(int)*IS_BUFFERSIZE);
      if (ptr == -1)
        exit(-1);
    } while(ptr==0);
    ssize_t bytestoread=(4-(ptr & 3)) & 3;
    while(bytestoread != 0) {
      ssize_t p=read(0, &((char *)buffer)[ptr], bytestoread);
      if (p == -1)
        exit(-1);
      bytestoread -= p;
      ptr += p;
    }
    length = ptr / 4;
    offset = 0;
  }
  
  return buffer[offset++];
}

void flushInts() {
  ssize_t bytestowrite=sizeof(int)*outoffset;
  ssize_t byteswritten=0;
  do {
    ssize_t n=write(IS_OUT_FD, &((char *)outbuffer)[byteswritten], bytestowrite);
    if (n == -1) {
      fprintf(stderr, "Write failure\n");
      exit(-1);
    }
    bytestowrite -= n;
    byteswritten += n;
  } while(bytestowrite != 0);
  outoffset = 0;
}

void putInt(int value) {
  if (outoffset>=IS_BUFFERSIZE) {
    flushInts();
  }
  outbuffer[outoffset++]=value;
}

void readClauses(Solver *solver) {
  vec<Lit> clause;
  int numvars = solver->nVars();
  bool haveClause = false;
  while(true) {
    int lit=getInt();
    if (lit!=0) {
      int var = abs(lit) - 1;
      while (var >= numvars) {
        numvars++;
        solver->newVar();
      }
      clause.push( (lit>0) ? mkLit(var) : ~mkLit(var));
      haveClause = true;
    } else {
      if (haveClause) {
        solver->addClause_(clause);
        haveClause = false;
        clause.clear();
      } else {
        //done with clauses
        return;
      }
    }
  }
}

void processCommands(SimpSolver *solver) {
  while(true) {
    int command=getInt();
    switch(command) {
    case IS_FREEZE: {
      int var=getInt()-1;
      solver->setFrozen(var, true);
      break;
    }
    case IS_RUNSOLVER: {
      vec<Lit> dummy;
      lbool ret = solver->solveLimited(dummy);
      if (ret == l_True) {
        putInt(IS_SAT);
        putInt(solver->nVars());
        putInt(0);
        for(int i=0;i<solver->nVars();i++) {
          putInt(solver->model[i]==l_True);
        }
      } else if (ret == l_False) {
        putInt(IS_UNSAT);
      } else {
        putInt(IS_INDETER);
      }
      flushInts();
      return;
    }
    default:
      fprintf(stderr, "Unreconized command\n");
      exit(-1);
    }
  }
}
  
void processSAT(SimpSolver *solver) {
  buffer=(int *) malloc(sizeof(int)*IS_BUFFERSIZE);
  offset=0;
  length=0;
  outbuffer=(int *) malloc(sizeof(int)*IS_BUFFERSIZE);
  outoffset=0;
  
  while(true) {
    double initial_time = cpuTime();    
    readClauses(solver);
    double parse_time = cpuTime();
    processCommands(solver);
    double finish_time = cpuTime();    
    printf("Parse time: %12.2f s Solve time:%12.2f s\n", parse_time-initial_time, finish_time-parse_time);
  }
}



//=================================================================================================
// Main:

int main(int argc, char** argv) {
  try {
    printf("c\nc This is glucose 4.0 --  based on MiniSAT (Many thanks to MiniSAT team)\nc\n");
    
    
    setUsageHelp("c USAGE: %s [options] <input-file> <result-output-file>\n\n  where input may be either in plain or gzipped DIMACS.\n");
    
    
#if defined(__linux__)
    fpu_control_t oldcw, newcw;
    _FPU_GETCW(oldcw); newcw = (oldcw & ~_FPU_EXTENDED) | _FPU_DOUBLE; _FPU_SETCW(newcw);
    //printf("c WARNING: for repeatability, setting FPU to use double precision\n");
#endif
    // Extra options:
    //
    IntOption    verb   ("MAIN", "verb",   "Verbosity level (0=silent, 1=some, 2=more).", 1, IntRange(0, 2));
    BoolOption   mod   ("MAIN", "model",   "show model.", false);
    IntOption    vv  ("MAIN", "vv",   "Verbosity every vv conflicts", 10000, IntRange(1,INT32_MAX));
    BoolOption   pre    ("MAIN", "pre",    "Completely turn on/off any preprocessing.", true);
    StringOption dimacs ("MAIN", "dimacs", "If given, stop after preprocessing and write the result to this file.");
    IntOption    cpu_lim("MAIN", "cpu-lim","Limit on CPU time allowed in seconds.\n", INT32_MAX, IntRange(0, INT32_MAX));
    IntOption    mem_lim("MAIN", "mem-lim","Limit on memory usage in megabytes.\n", INT32_MAX, IntRange(0, INT32_MAX));
    
    BoolOption    opt_certified      (_certified, "certified",    "Certified UNSAT using DRUP format", false);
    StringOption  opt_certified_file      (_certified, "certified-output",    "Certified UNSAT output file", "NULL");
         
    parseOptions(argc, argv, true);
    
    SimpSolver  S;
    S.parsing = 1;
    S.verbosity = verb;
    S.verbEveryConflicts = vv;
    S.showModel = mod;
    solver = &S;
    // Use signal handlers that forcibly quit until the solver will be
    // able to respond to interrupts:
    signal(SIGINT, SIGINT_exit);
    signal(SIGXCPU,SIGINT_exit);


    // Set limit on CPU-time:
    if (cpu_lim != INT32_MAX){
      rlimit rl;
      getrlimit(RLIMIT_CPU, &rl);
      if (rl.rlim_max == RLIM_INFINITY || (rlim_t)cpu_lim < rl.rlim_max){
        rl.rlim_cur = cpu_lim;
        if (setrlimit(RLIMIT_CPU, &rl) == -1)
          printf("c WARNING! Could not set resource limit: CPU-time.\n");
      } }
    
    // Set limit on virtual memory:
    if (mem_lim != INT32_MAX){
      rlim_t new_mem_lim = (rlim_t)mem_lim * 1024*1024;
      rlimit rl;
      getrlimit(RLIMIT_AS, &rl);
      if (rl.rlim_max == RLIM_INFINITY || new_mem_lim < rl.rlim_max){
        rl.rlim_cur = new_mem_lim;
        if (setrlimit(RLIMIT_AS, &rl) == -1)
          printf("c WARNING! Could not set resource limit: Virtual memory.\n");
      } }
    
    //do solver stuff here
    processSAT(&S);
    
    printf("c |  Number of variables:  %12d                                                                   |\n", S.nVars());
    printf("c |  Number of clauses:    %12d                                                                   |\n", S.nClauses());
    
    
    // Change to signal-handlers that will only notify the solver and allow it to terminate
    // voluntarily:
    signal(SIGINT, SIGINT_interrupt);
    signal(SIGXCPU,SIGINT_interrupt);
    
    S.parsing = 0;

#ifdef NDEBUG
    exit(0);
#else
    return (0);
#endif
  } catch (OutOfMemoryException&){
    printf("c =========================================================================================================\n");
    printf("INDETERMINATE\n");
    exit(0);
  }
}
