/* ----------------------------------------------------------------------
   LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
   www.cs.sandia.gov/~sjplimp/lammps.html
   Steve Plimpton, sjplimp@sandia.gov, Sandia National Laboratories

   Copyright (2003) Sandia Corporation.  Under the terms of Contract
   DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government retains
   certain rights in this software.  This software is distributed under 
   the GNU General Public License.

   See the README file in the top-level LAMMPS directory.
------------------------------------------------------------------------- */

#ifndef PAIR_LJ_COUL_H
#define PAIR_LJ_COUL_H

#include "pair.h"

namespace LAMMPS_NS {

class PairLJCoul : public Pair {
 public:
  double cut_coul;

  PairLJCoul(class LAMMPS *);
  virtual ~PairLJCoul();
  virtual void compute(int, int);
  virtual void settings(int, char **);
  void coeff(int, char **);
  double init_one(int, int);
  virtual void init_style();
  void write_restart(FILE *);
  void read_restart(FILE *);
  
  virtual void write_restart_settings(FILE *);
  virtual void read_restart_settings(FILE *);
  virtual void single(int, int, int, int, double, double, double, int, One &);
  virtual void *extract_ptr(char *);
  virtual void extract_long(double *);

  void compute_inner();
  void compute_middle();
  void compute_outer(int, int);

 protected:
  double cut_lj_global;
  double **cut_lj, **cut_lj_read, **cut_ljsq;
  double cut_coulsq;
  double **epsilon_read, **epsilon, **sigma_read, **sigma;
  double **lj1, **lj2, **lj3, **lj4, **offset;
  double *cut_respa;
  double g_ewald;
  int ewald_order, ewald_off;

  double tabinnersq;
  double *rtable, *drtable, *ftable, *dftable, *ctable, *dctable;
  double *etable, *detable, *ptable, *dptable, *vtable, *dvtable;
  int ncoulshiftbits, ncoulmask;

  void options(char **arg, int order);
  void allocate();
  void init_tables();
  void free_tables();
};

}

#endif
