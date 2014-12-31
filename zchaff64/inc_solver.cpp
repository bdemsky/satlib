/* =========FOR INTERNAL USE ONLY. NO DISTRIBUTION PLEASE ========== */

/*********************************************************************
 Copyright 2000-2004, Princeton University.  All rights reserved. 
 By using this software the USER indicates that he or she has read, 
 understood and will comply with the following:

 --- Princeton University hereby grants USER nonexclusive permission 
 to use, copy and/or modify this software for internal, noncommercial,
 research purposes only. Any distribution, including commercial sale 
 or license, of this software, copies of the software, its associated 
 documentation and/or modifications of either is strictly prohibited 
 without the prior consent of Princeton University.  Title to copyright
 to this software and its associated documentation shall at all times 
 remain with Princeton University.  Appropriate copyright notice shall 
 be placed on all software copies, and a complete copy of this notice 
 shall be included in all copies of the associated documentation.  
 No right is  granted to use in advertising, publicity or otherwise 
 any trademark,  service mark, or the name of Princeton University. 


 --- This software and any associated documentation is provided "as is" 

 PRINCETON UNIVERSITY MAKES NO REPRESENTATIONS OR WARRANTIES, EXPRESS 
 OR IMPLIED, INCLUDING THOSE OF MERCHANTABILITY OR FITNESS FOR A 
 PARTICULAR PURPOSE, OR THAT  USE OF THE SOFTWARE, MODIFICATIONS, OR 
 ASSOCIATED DOCUMENTATION WILL NOT INFRINGE ANY PATENTS, COPYRIGHTS, 
 TRADEMARKS OR OTHER INTELLECTUAL PROPERTY RIGHTS OF A THIRD PARTY.  

 Princeton University shall not be liable under any circumstances for 
 any direct, indirect, special, incidental, or consequential damages 
 with respect to any claim by USER or any third party on account of 
 or arising from the use, or inability to use, this software or its 
 associated documentation, even if Princeton University has been advised
 of the possibility of those damages.
*********************************************************************/
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cstdio>

#include <set>
#include <vector>
#include <dirent.h>
#include "SAT.h"
#include "solver_interface.h"
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>

using namespace std;




static inline double cpuTime(void) {
  struct rusage ru;
  getrusage(RUSAGE_SELF, &ru);
  return (double)ru.ru_utime.tv_sec + (double)ru.ru_utime.tv_usec / 1000000;
}

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

int numvars=0;
void readClauses(SAT_Manager solver) {
  vector<int> clause;
  bool haveClause = false;
  while(true) {
    int lit=getInt();
    if (lit!=0) {
      int var = abs(lit);
      while (var > numvars) {
        numvars++;
        SAT_AddVariable(solver);
      }
      int shvar=var << 1;
      clause.push_back( (lit>0) ? shvar : shvar+1);
      haveClause = true;
    } else {
      if (haveClause) {
        SAT_AddClause(solver, & clause.begin()[0], clause.size());
        haveClause = false;
        clause.clear();
      } else {
        //done with clauses
        return;
      }
    }
  }
}
bool first=true;;

void processCommands(SAT_Manager solver) {
  while(true) {
    int command=getInt();
    switch(command) {
    case IS_FREEZE: {
      int var=getInt();
      break;
    }
    case IS_RUNSOLVER: {
      if (!first) {
        SAT_Reset(solver);
      }
      first=false;
      int ret = SAT_Solve(solver);
      
      if (ret == SATISFIABLE) {
        putInt(IS_SAT);
        putInt(numvars);
        for(int i=1;i<=numvars;i++) {
          putInt(SAT_GetVarAsgnment(solver, i)==1);
        }
      } else if (ret == UNSATISFIABLE) {
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
void processSAT(SAT_Manager solver) {
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


int main(int argc, char ** argv) {
  SAT_Manager mng = SAT_InitManager();
  processSAT(mng);
  return 0;
}
