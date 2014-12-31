/*-------------------------------------------------------------------------*/
/* Copyright 2010-2014 Armin Biere Johannes Kepler University Linz Austria */
/*-------------------------------------------------------------------------*/

#include "lglib.h"

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "solver_interface.h"

static LGL * lgl4sigh;
static int catchedsig, verbose, ignmissingheader, ignaddcls;

static void (*sig_int_handler)(int);
static void (*sig_segv_handler)(int);
static void (*sig_abrt_handler)(int);
static void (*sig_term_handler)(int);
static void (*sig_bus_handler)(int);
static void (*sig_alrm_handler)(int);

static void resetsighandlers (void) {
  (void) signal (SIGINT, sig_int_handler);
  (void) signal (SIGSEGV, sig_segv_handler);
  (void) signal (SIGABRT, sig_abrt_handler);
  (void) signal (SIGTERM, sig_term_handler);
  (void) signal (SIGBUS, sig_bus_handler);
}

static void caughtsigmsg (int sig) {
  if (verbose < 0) return;
  printf ("c\nc CAUGHT SIGNAL %d", sig);
  switch (sig) {
    case SIGINT: printf (" SIGINT"); break;
    case SIGSEGV: printf (" SIGSEGV"); break;
    case SIGABRT: printf (" SIGABRT"); break;
    case SIGTERM: printf (" SIGTERM"); break;
    case SIGBUS: printf (" SIGBUS"); break;
    case SIGALRM: printf (" SIGALRM"); break;
    default: break;
  }
  printf ("\nc\n");
  fflush (stdout);
}

static void catchsig (int sig) {
  if (!catchedsig) {
    catchedsig = 1;
    caughtsigmsg (sig);
    fputs ("c s UNKNOWN\n", stdout);
    fflush (stdout);
    if (verbose >= 0) {
      lglflushtimers (lgl4sigh);
      lglstats (lgl4sigh);
      caughtsigmsg (sig);
    }
  }
  resetsighandlers ();
  if (!getenv ("LGLNABORT")) raise (sig); else exit (1);
}

static void setsighandlers (void) {
  sig_int_handler = signal (SIGINT, catchsig);
  sig_segv_handler = signal (SIGSEGV, catchsig);
  sig_abrt_handler = signal (SIGABRT, catchsig);
  sig_term_handler = signal (SIGTERM, catchsig);
  sig_bus_handler = signal (SIGBUS, catchsig);
}

static int timelimit = -1, caughtalarm = 0;

static void catchalrm (int sig) {
  assert (sig == SIGALRM);
  if (!caughtalarm) {
    caughtalarm = 1;
    caughtsigmsg (sig);
    if (timelimit >= 0) {
      printf ("c time limit of %d reached after %.1f seconds\nc\n",
              timelimit, lglsec (lgl4sigh));
      fflush (stdout);
    }
  }
}

static int checkalarm (void * ptr) {
  assert (ptr == (void*) &caughtalarm);
  return caughtalarm;
}

static int primes[] = {
  200000033, 200000039, 200000051, 200000069, 200000081,
};

static int nprimes = sizeof primes / sizeof *primes;



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

void readClauses(LGL *solver) {
  bool haveClause = false;
  while(true) {
    int lit=getInt();
    if (lit!=0) {
      haveClause = true;
      lgladd(solver, lit);
    } else {
      if (haveClause) {
        lgladd(solver, 0);
        haveClause = false;
      } else {
        //done with clauses
        return;
      }
    }
  }
}

void processCommands(LGL *solver) {
  while(true) {
    int command=getInt();
    switch(command) {
    case IS_FREEZE: {
      int var=getInt();
      lglfreeze(solver, var);
      break;
    }
    case IS_RUNSOLVER: {
      int ret = lglsat(solver);
      if (ret == 10) {
        putInt(IS_SAT);
        int numvars=lglmaxvar(solver);
        putInt(numvars);
        for(int i=1;i<=numvars;i++) {
          putInt(lglderef(solver, i) > 0);
        }
      } else if (ret == 20) {
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
  
void processSAT(LGL *solver) {
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



int main (int argc, char ** argv) {
  int res, i, j, val, len, lineno, simponly, count;
  const char * pname, * match, * p, * err, * thanks;
  int maxvar, lit, nopts, simplevel;
  FILE * pfile;
  char * tmp;
  LGL * lgl;
  lineno = 1;
  res = simponly = simplevel = 0;
  pname = thanks = 0;
  lgl4sigh = lgl = lglinit ();
  setsighandlers ();
  for (i = 1; i < argc; i++) {
    if (!strcmp (argv[i], "-h") || !strcmp (argv[i], "--help")) {
      printf ("usage: lingeling [<option> ...][<file>[.gz]]\n");
      printf ("\n");
      printf ("where <option> is one of the following:\n");
      printf ("\n");
      printf ("-q               be quiet (same as '--verbose=-1')\n");
      printf ("-s               only simplify and print to output file\n");
      printf ("-O<L>            set simplification level to <L>\n");
      printf ("-p <options>     read options from file\n");
      printf ("\n");
      printf ("-t <seconds>     set time limit\n");
      printf ("\n");
      printf ("\n");
      printf ("-h|--help        print command line option summary\n");
      printf ("-f|--force       force reading even without header\n");
      printf ("-i|--ignore      ignore additional clauses\n");
      printf ("-r|--ranges      print value ranges of options\n");
      printf ("-d|--defaults    print default values of options\n");
      printf ("-P|--pcs         print (full) PCS file\n");
      printf ("--pcs-mixed      print mixed PCS file\n");
      printf ("--pcs-reduced    print reduced PCS file\n");
      printf ("-e|--embedded    ditto but in an embedded format print\n");
      printf ("-n|--no-witness   do not print solution (see '--witness')\n");
      printf ("\n");
      printf ("--thanks=<whom>  alternative way of specifying the seed\n");
      printf ("                 (inspired by Vampire)\n");
      printf ("\n");
      printf (
"The following options can also be used in the form '--<name>=<int>',\n"
"just '--<name>' for increment and '--no-<name>' for zero.  They\n"
"can be embedded into the CNF file, set through the API or capitalized\n"
"with prefix 'LGL' instead of '--' through environment variables.\n"
"Their default values are displayed in square brackets.\n");
      printf ("\n");
      lglusage (lgl);
      goto DONE;
    } else if (!strcmp (argv[i], "-s")) simponly = 1;
    else if (argv[i][0] == '-' && argv[i][1] == 'O') {
      if (simplevel > 0) {
	fprintf (stderr, "*** lingeling error: multiple '-O..' options\n");
	res = 1;
	goto DONE;
      }
      if ((simplevel = atoi (argv[i] + 2)) <= 0) {
	fprintf (stderr,
	   "*** lingeling error: invalid '%s' option\n", argv[i]);
	res = 1;
	goto DONE;
      }
    } else if (!strcmp (argv[i], "-q")) lglsetopt (lgl, "verbose", -1);
    else if (!strcmp (argv[i], "-p")) {
      if (++i == argc) {
	fprintf (stderr, "*** lingeling error: argument to '-p' missing\n");
	res = 1;
	goto DONE;
      } 
      if (pname) {
	fprintf (stderr, 
	         "*** lingeling error: "
		 "multiple option files '%s' and '%s'\n",
		 pname, argv[i]);
	res = 1;
	goto DONE;
      }
      pname = argv[i];
    } else if (!strcmp (argv[i], "-t")) {
      if (++i == argc) {
	fprintf (stderr, "*** lingeling error: argument to '-t' missing\n");
	res = 1;
	goto DONE;
      }
      if (timelimit >= 0) {
	fprintf (stderr, "*** lingeling error: timit limit set twice\n");
	res = 1;
	goto DONE;
      }
      for (p = argv[i]; *p && isdigit (*p); p++) 
	;
      if (p == argv[i] || *p || (timelimit = atoi (argv[i])) < 0) {
	fprintf (stderr, 
	  "*** lingeling error: invalid time limit '-t %s'\n", argv[i]);
	res = 1;
	goto DONE;
      }
    } else if (!strcmp (argv[i], "-d") || !strcmp (argv[i], "--defaults")) {
      lglopts (lgl, "", 0);
      goto DONE;
    } else if (!strcmp (argv[i], "-e") || !strcmp (argv[i], "--embedded")) {
      lglopts (lgl, "c ", 1);
      goto DONE;
    } else if (!strcmp (argv[i], "-r") || !strcmp (argv[i], "--ranges")) {
      lglrgopts (lgl);
      goto DONE;
    } else if (!strcmp (argv[i], "-P") || !strcmp (argv[i], "--pcs")) {
      printf ("# generated by 'lingeling --pcs'\n");
      printf ("# version %s\n", lglversion ());
      lglpcs (lgl, 0);
      goto DONE;
    } else if (!strcmp (argv[i], "--pcs-mixed")) {
      printf ("# generated by 'lingeling --pcs-mixed'\n");
      printf ("# version %s\n", lglversion ());
      lglpcs (lgl, 1);
      goto DONE;
    } else if (!strcmp (argv[i], "--pcs-reduced")) {
      printf ("# generated by 'lingeling --pcs-reduced'\n");
      printf ("# version %s\n", lglversion ());
      lglpcs (lgl, -1);
      goto DONE;
    } else if (!strcmp (argv[i], "-f") || !strcmp (argv[i], "--force")) {
      ignmissingheader = 1;
    } else if (!strcmp (argv[i], "-i") || !strcmp (argv[i], "--ignore")) {
      ignaddcls = 1;
    } else if (!strcmp (argv[i], "-n") || !strcmp (argv[i], "no-witness")) {
      lglsetopt (lgl, "witness", 0);
    } else if (argv[i][0] == '-') {
      if (argv[i][1] == '-') {
	match = strchr (argv[i] + 2, '=');
	if (match) {
	  p = match + 1;
	  if (*p == '-') p++;	// TODO for what is this useful again?
	  len = p - argv[i];
	  if (!strncmp (argv[i], "--write-api-trace=", len)) {
	    // TODO not handled yet ...
	    continue;
	  } else if (!strncmp (argv[i], "--thanks=", len)) {
	    thanks = match + 1;
	    continue;
	  } else if (!isdigit (*p)) {
ERR:
            fprintf (stderr,
	      "*** lingeling error: invalid command line option '%s'\n",
	      argv[i]);
	    res = 1;
	    goto DONE;
	  }
	  while (*++p) if (!isdigit (*p)) goto ERR;
	  len = match - argv[i] - 2;
	  tmp = malloc (len + 1);
	  j = 0;
	  for (p = argv[i] + 2; *p != '='; p++) tmp[j++] = *p;
	  tmp[j] = 0;
	  val = atoi (match + 1);
	} else if (!strncmp (argv[i], "--no-", 5)) {
	  tmp = strdup (argv[i] + 5);
	  val = 0;
	} else {
	  tmp = strdup (argv[i] + 2);
	  val = lglgetopt (lgl, tmp) + 1;
	}
	if (!lglhasopt (lgl, tmp)) { free (tmp); goto ERR; }
	lglsetopt (lgl, tmp, val);
	free (tmp);
      } else {
	if (argv[i][2]) goto ERR;
	if (!lglhasopt (lgl, argv[i] + 1)) goto ERR;
	val = lglgetopt (lgl, argv[i] + 1) + 1;
	lglsetopt (lgl, argv[i] + 1, val);
      }
    }
  }
  verbose = lglgetopt (lgl, "verbose");
  if (verbose >= 0) {
    lglbnr ("Lingeling SAT Solver", "c ", stdout);
    if (simponly) printf ("c simplifying only\n");
    if (simponly) fflush (stdout);
    lglsetopt (lgl, "trep", 1);
  }
  if (thanks) {
    unsigned seed = 0, i = 0, ch;
    int iseed;
    for (p = thanks; (ch = *p); p++) {
      seed += primes[i++] * ch;
      if (i == nprimes) i = 0;
    }
    if (seed >= (unsigned) INT_MAX) seed >>= 1;
    assert (seed <= (unsigned) INT_MAX);
    iseed = (int) seed;
    assert (iseed >= 0);
    if (verbose)
      printf ("c will have to thank %s (--seed=%d)\nc\n",
	thanks, iseed);
    lglsetopt (lgl, "seed", iseed);
  }
  if (verbose >= 2) {
   printf ("c\nc options after command line parsing:\nc\n");
   lglopts (lgl, "c ", 0);
   printf ("c\n");
   lglsizes (lgl);
   printf ("c\n");
  }

  if (pname) {
    pfile = fopen (pname, "r");
    if (!pfile) {
      fprintf (stderr,
        "*** lingeling error: can not read option file %s\n", pname);
      res = 1;
      goto DONE;
    }
    if (verbose >= 0) {
      printf ("c reading options file %s\n", pname);
      fflush (stdout);
    }
    nopts = lglreadopts (lgl, pfile);
    if (verbose >= 0) 
      printf ("c read and set %d options\nc\n", nopts), fflush (stdout);
    fclose (pfile);
  }

  fflush (stdout);

  if (verbose >= 1) {
    printf ("c\n");
    if (verbose >= 2) printf ("c final options:\nc\n");
    lglopts (lgl, "c ", 0);
  }
  if (timelimit >= 0) {
    if (verbose >= 0) {
      printf ("c\nc setting time limit of %d seconds\n", timelimit);
      fflush (stdout);
    }
    lglseterm (lgl, checkalarm, &caughtalarm);
    sig_alrm_handler = signal (SIGALRM, catchalrm);
    alarm (timelimit);
  }

  processSAT(lgl);
  
  if (timelimit >= 0) {
    caughtalarm = 0;
    (void) signal (SIGALRM, sig_alrm_handler);
  }
  if (verbose >= 0) fputs ("c\n", stdout), lglstats (lgl);
DONE:
  resetsighandlers ();
  lgl4sigh = 0;
  lglrelease (lgl);
  return 0;
}
