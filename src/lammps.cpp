/* ----------------------------------------------------------------------
   LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
   http://lammps.sandia.gov, Sandia National Laboratories
   Steve Plimpton, sjplimp@sandia.gov

   Copyright (2003) Sandia Corporation.  Under the terms of Contract
   DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government retains
   certain rights in this software.  This software is distributed under 
   the GNU General Public License.

   See the README file in the top-level LAMMPS directory.
------------------------------------------------------------------------- */

#include "mpi.h"
#include "string.h"
#include "lammps.h"
#include "memory.h"
#include "error.h"
#include "universe.h"
#include "input.h"
#include "atom.h"
#include "update.h"
#include "neighbor.h"
#include "comm.h"
#include "domain.h"
#include "force.h"
#include "modify.h"
#include "group.h"
#include "output.h"
#include "accelerator_cuda.h"
#include "timer.h"

using namespace LAMMPS_NS;

/* ----------------------------------------------------------------------
   start up LAMMPS
   allocate fundamental classes (memory, error, universe, input)
   parse input switches
   initialize communicators, screen & logfile output
   input is allocated at end after MPI info is setup
------------------------------------------------------------------------- */

LAMMPS::LAMMPS(int narg, char **arg, MPI_Comm communicator)
{
  memory = new Memory(this);
  error = new Error(this);
  universe = new Universe(this,communicator);
  output = NULL;

  screen = NULL;
  logfile = NULL;

  // create CUDA class if USER-CUDA installed, else dummy

  cuda = new Cuda(this);
  if (!cuda->cuda_exists) {
    delete cuda;
    cuda = NULL;
  }

  // parse input switches

  int inflag = 0;
  int screenflag = 0;
  int logflag = 0;

  if (cuda) {
    int n = strlen("cuda") + 1;
    asuffix = new char[n];
    strcpy(asuffix,"cuda");
    accelerator = 1;
  } else {
    asuffix = NULL;
    accelerator = 0;
  }

  int iarg = 1;

  while (iarg < narg) {
    if (strcmp(arg[iarg],"-partition") == 0 || 
	strcmp(arg[iarg],"-p") == 0) {
      universe->existflag = 1;
      if (iarg+2 > narg) error->universe_all("Invalid command-line argument");
      iarg++;
      while (iarg < narg && arg[iarg][0] != '-') {
	universe->add_world(arg[iarg]);
	iarg++;
      }
    } else if (strcmp(arg[iarg],"-in") == 0 || 
	       strcmp(arg[iarg],"-i") == 0) {
      if (iarg+2 > narg) error->universe_all("Invalid command-line argument");
      inflag = iarg + 1;
      iarg += 2;
    } else if (strcmp(arg[iarg],"-screen") == 0 || 
	       strcmp(arg[iarg],"-s") == 0) {
      if (iarg+2 > narg) error->universe_all("Invalid command-line argument");
      screenflag = iarg + 1;
      iarg += 2;
    } else if (strcmp(arg[iarg],"-log") == 0 || 
	       strcmp(arg[iarg],"-l") == 0) {
      if (iarg+2 > narg) error->universe_all("Invalid command-line argument");
      logflag = iarg + 1;
      iarg += 2;
    } else if (strcmp(arg[iarg],"-var") == 0 || 
	       strcmp(arg[iarg],"-v") == 0) {
      if (iarg+3 > narg) error->universe_all("Invalid command-line argument");
      iarg += 2;
      while (iarg < narg && arg[iarg][0] != '-') iarg++;
    } else if (strcmp(arg[iarg],"-echo") == 0 || 
	       strcmp(arg[iarg],"-e") == 0) {
      if (iarg+2 > narg) error->universe_all("Invalid command-line argument");
      iarg += 2;
    } else if (strcmp(arg[iarg],"-accel") == 0 || 
	       strcmp(arg[iarg],"-a") == 0) {
      if (iarg+2 > narg) error->universe_all("Invalid command-line argument");
      if (strcmp(arg[iarg+1],"none") == 0) {
	delete [] asuffix;
	asuffix = NULL;
	accelerator = 0;
      } else if (strcmp(arg[iarg+1],"opt") == 0 ||
		 strcmp(arg[iarg+1],"gpu") == 0 ||
		 strcmp(arg[iarg+1],"cuda") == 0) {
	int n = strlen(arg[iarg+1]) + 1;
	asuffix = new char[n];
	strcpy(asuffix,arg[iarg+1]);
	accelerator = 1;
      }
      if (strcmp(asuffix,"cuda") == 0 && !cuda)
	error->all("Cannot use -a cuda without USER-CUDA package installed");
      iarg += 2;
    } else error->universe_all("Invalid command-line argument");
  }

  // if no partition command-line switch, universe is one world w/ all procs

  if (universe->existflag == 0) universe->add_world(NULL);

  // sum of procs in all worlds must equal total # of procs

  if (!universe->consistent())
    error->universe_all("Processor partitions are inconsistent");

  // universe cannot use stdin for input file

  if (universe->existflag && inflag == 0)
    error->universe_all("Must use -in switch with multiple partitions");

  // set universe screen and logfile

  if (universe->me == 0) {
    if (screenflag == 0)
      universe->uscreen = stdout;
    else if (strcmp(arg[screenflag],"none") == 0)
      universe->uscreen = NULL;
    else {
      universe->uscreen = fopen(arg[screenflag],"w");
      if (universe->uscreen == NULL) 
	error->universe_one("Cannot open universe screen file");
    }
    if (logflag == 0) {
      universe->ulogfile = fopen("log.lammps","w");
      if (universe->ulogfile == NULL) 
	error->universe_one("Cannot open log.lammps");
    } else if (strcmp(arg[logflag],"none") == 0)
      universe->ulogfile = NULL;
    else {
      universe->ulogfile = fopen(arg[logflag],"w");
      if (universe->ulogfile == NULL) 
	error->universe_one("Cannot open universe log file");
    }
  }

  if (universe->me > 0) {
    if (screenflag == 0) universe->uscreen = stdout;
    else universe->uscreen = NULL;
    universe->ulogfile = NULL;
  }

  // universe does not exist on its own, only a single world
  // inherit settings from universe
  // set world screen, logfile, communicator, infile
  // open input script if from file

  if (universe->existflag == 0) {
    screen = universe->uscreen;
    logfile = universe->ulogfile;
    world = universe->uworld;
    infile = NULL;

    if (universe->me == 0) {
      if (inflag == 0) infile = stdin;
      else infile = fopen(arg[inflag],"r");
      if (infile == NULL) {
	char str[128];
	sprintf(str,"Cannot open input script %s",arg[inflag]);
	error->one(str);
      }
    }

    if (universe->me == 0) {
      if (screen) fprintf(screen,"LAMMPS (%s)\n",universe->version);
      if (logfile) fprintf(logfile,"LAMMPS (%s)\n",universe->version);
    }

  // universe is one or more worlds
  // split into separate communicators
  // set world screen, logfile, communicator, infile
  // open input script

  } else {
    int me;
    MPI_Comm_split(universe->uworld,universe->iworld,0,&world);
    MPI_Comm_rank(world,&me);

    if (me == 0) {
      if (screenflag == 0) {
	char str[32];
	sprintf(str,"screen.%d",universe->iworld);
	screen = fopen(str,"w");
	if (screen == NULL) error->one("Cannot open screen file");
      } else if (strcmp(arg[screenflag],"none") == 0)
	screen = NULL;
      else {
	char str[128];
	sprintf(str,"%s.%d",arg[screenflag],universe->iworld);
	screen = fopen(str,"w");
	if (screen == NULL) error->one("Cannot open screen file");
      }
    } else screen = NULL;
    
    if (me == 0) {
      if (logflag == 0) {
	char str[32];
	sprintf(str,"log.lammps.%d",universe->iworld);
	logfile = fopen(str,"w");
	if (logfile == NULL) error->one("Cannot open logfile");
      } else if (strcmp(arg[logflag],"none") == 0)
	logfile = NULL;
      else {
	char str[128];
	sprintf(str,"%s.%d",arg[logflag],universe->iworld);
	logfile = fopen(str,"w");
	if (logfile == NULL) error->one("Cannot open logfile");
      }
    } else logfile = NULL;
    
    if (me == 0) {
      infile = fopen(arg[inflag],"r");
      if (infile == NULL) {
	char str[128];
	sprintf(str,"Cannot open input script %s",arg[inflag]);
	error->one(str);
      }
    } else infile = NULL;
    
    // screen and logfile messages for universe and world
    
    if (universe->me == 0) {
      if (universe->uscreen) {
	fprintf(universe->uscreen,"LAMMPS (%s)\n",universe->version);
	fprintf(universe->uscreen,"Running on %d partitions of processors\n",
		universe->nworlds);
      }
      if (universe->ulogfile) {
	fprintf(universe->ulogfile,"LAMMPS (%s)\n",universe->version);
	fprintf(universe->ulogfile,"Running on %d partitions of processors\n",
		universe->nworlds);
      }
    }
    
    if (me == 0) {
      if (screen) {
	fprintf(screen,"LAMMPS (%s)\n",universe->version);
	fprintf(screen,"Processor partition = %d\n",universe->iworld);
      }
      if (logfile) {
	fprintf(logfile,"LAMMPS (%s)\n",universe->version);
	fprintf(logfile,"Processor partition = %d\n",universe->iworld);
      }
    }
  }

  // check datatype settings in lmptype.h

  if (sizeof(smallint) != sizeof(int))
    error->all("Smallint setting in lmptype.h is invalid");
  if (sizeof(tagint) < sizeof(smallint))
    error->all("Tagint setting in lmptype.h is invalid");
  if (sizeof(bigint) < sizeof(tagint))
    error->all("Bigint setting in lmptype.h is invalid");

  int mpisize;
  MPI_Type_size(MPI_LMP_TAGINT,&mpisize);
  if (mpisize != sizeof(tagint))
      error->all("MPI_LMP_TAGINT and tagint in lmptype.h are not compatible");
  MPI_Type_size(MPI_LMP_BIGINT,&mpisize);
  if (mpisize != sizeof(bigint))
      error->all("MPI_LMP_BIGINT and bigint in lmptype.h are not compatible");

  // allocate input class now that MPI is fully setup

  input = new Input(this,narg,arg);

  // allocate top-level classes

  create();
}

/* ----------------------------------------------------------------------
   shutdown LAMMPS
   delete top-level classes
   close screen and log files in world and universe
   output files were already closed in destroy()
   delete fundamental classes
------------------------------------------------------------------------- */

LAMMPS::~LAMMPS()
{
  destroy();

  if (universe->nworlds == 1) {
    if (logfile) fclose(logfile);
  } else {
    if (screen && screen != stdout) fclose(screen);
    if (logfile) fclose(logfile);
    if (universe->ulogfile) fclose(universe->ulogfile);
  }

  if (world != universe->uworld) MPI_Comm_free(&world);

  delete [] asuffix;
  delete cuda;

  delete input;
  delete universe;
  delete error;
  delete memory;
}

/* ----------------------------------------------------------------------
   allocate single instance of top-level classes
   fundamental classes are allocated in constructor
   some classes have accelerator variants
------------------------------------------------------------------------- */

void LAMMPS::create()
{
  atom = new Atom(this);

  if (cuda) neighbor = new NeighborCuda(this);
  else neighbor = new Neighbor(this);

  if (cuda) comm = new CommCuda(this);
  else comm = new Comm(this);

  if (cuda) domain = new DomainCuda(this);
  else domain = new Domain(this);

  group = new Group(this);
  force = new Force(this);    // must be after group, to create temperature

  if (cuda) modify = new ModifyCuda(this);
  else modify = new Modify(this);

  output = new Output(this);  // must be after group, so "all" exists
                              // must be after modify so can create Computes
  update = new Update(this);  // must be after output, force, neighbor
  timer = new Timer(this);
}

/* ----------------------------------------------------------------------
   initialize top-level classes
------------------------------------------------------------------------- */

void LAMMPS::init()
{
  if (cuda) cuda->accelerator(0,NULL);
 
  update->init();
  force->init();         // pair must come after update due to minimizer
  domain->init();
  atom->init();          // atom must come after force and domain
                         //   atom deletes extra array
                         //   used by fix shear_history::unpack_restart()
                         //   when force->pair->gran_history creates fix ??
                         //   atom_vec init uses deform_vremap
  modify->init();        // modify must come after update, force, atom, domain
  neighbor->init();      // neighbor must come after force, modify
  comm->init();          // comm must come after force, modify, neighbor, atom
  output->init();        // output must come after domain, force, modify
  timer->init();
}

/* ----------------------------------------------------------------------
   delete single instance of top-level classes
   fundamental classes are deleted in destructor
------------------------------------------------------------------------- */

void LAMMPS::destroy()
{
  delete update;
  delete neighbor;
  delete comm;
  delete force;
  delete group;
  delete output;
  delete modify;          // modify must come after output, force, update
                          //   since they delete fixes
  delete domain;          // domain must come after modify
                          //   since fix destructors access domain
  delete atom;            // atom must come after modify, neighbor
                          //   since fixes delete callbacks in atom
  delete timer;
}
