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

#include "math.h"
#include "stdlib.h"
#include "pair_sph_taitwater_multiphase.h"
#include "atom.h"
#include "force.h"
#include "comm.h"
#include "neigh_list.h"
#include "memory.h"
#include "error.h"
#include "domain.h"
#include "sph_kernel_quintic.h"

using namespace LAMMPS_NS;

/* ---------------------------------------------------------------------- */

PairSPHTaitwaterMultiphase::PairSPHTaitwaterMultiphase(LAMMPS *lmp) : Pair(lmp)
{
  restartinfo = 0;
  first = 1;
}

/* ---------------------------------------------------------------------- */

PairSPHTaitwaterMultiphase::~PairSPHTaitwaterMultiphase() {
  if (allocated) {
    memory->destroy(setflag);
    memory->destroy(cutsq);

    memory->destroy(cut);
    memory->destroy(rho0);
    memory->destroy(soundspeed);
    memory->destroy(B);
    memory->destroy(viscosity);
    memory->destroy(gamma);
    memory->destroy(rbackground);
  }
}

/* ---------------------------------------------------------------------- */

void PairSPHTaitwaterMultiphase::compute(int eflag, int vflag) {
  int i, j, ii, jj, inum, jnum, itype, jtype;
  double xtmp, ytmp, ztmp, delx, dely, delz, fpair;

  int *ilist, *jlist, *numneigh, **firstneigh;
  double vxtmp, vytmp, vztmp, imass, jmass, fi, fj, fvisc, h, ih, ihsq, velx, vely, velz;
  double rsq, tmp, wfd, delVdotDelR, deltaE;

  if (eflag || vflag)
    ev_setup(eflag, vflag);
  else
    evflag = vflag_fdotr = 0;

  double **v = atom->vest;
  double **x = atom->x;
  double **f = atom->f;
  double *rho = atom->rho;
  double *rmass = atom->rmass;
  double *mass = atom->mass;
  int *type = atom->type;
  int nlocal = atom->nlocal;
  int newton_pair = force->newton_pair;
  int rmass_flag = atom->rmass_flag;
  // check consistency of pair coefficients

  if (first) {
    for (i = 1; i <= atom->ntypes; i++) {
      for (j = 1; i <= atom->ntypes; i++) {
        if (cutsq[i][j] > 1.e-32) {
          if (!setflag[i][i] || !setflag[j][j]) {
            if (comm->me == 0) {
              printf(
                  "SPH particle types %d and %d interact with cutoff=%g, but not all of their single particle properties are set.\n",
                  i, j, sqrt(cutsq[i][j]));
            }
          }
        }
      }
    }
    first = 0;
  }

  inum = list->inum;
  ilist = list->ilist;
  numneigh = list->numneigh;
  firstneigh = list->firstneigh;

  // loop over neighbors of my atoms

  for (ii = 0; ii < inum; ii++) {
    i = ilist[ii];
    xtmp = x[i][0];
    ytmp = x[i][1];
    ztmp = x[i][2];
    vxtmp = v[i][0];
    vytmp = v[i][1];
    vztmp = v[i][2];
    itype = type[i];
    jlist = firstneigh[i];
    jnum = numneigh[i];

    if (rmass_flag) {
      imass = rmass[i];
    } else {
      imass = mass[itype];
    }


    double pi = sph_pressure(B[itype], rho0[itype], gamma[itype], rbackground[itype], rho[i]);
    fi = pi / (rho[i] * rho[i]);

    for (jj = 0; jj < jnum; jj++) {
      j = jlist[jj];
      j &= NEIGHMASK;

      delx = xtmp - x[j][0];
      dely = ytmp - x[j][1];
      delz = ztmp - x[j][2];
      rsq = delx * delx + dely * dely + delz * delz;
      jtype = type[j];
      if (rmass_flag) {
	jmass = rmass[j];
      } else {
 	jmass = mass[jtype];
      }
      jmass = rmass[j];

      if (rsq < cutsq[itype][jtype]) {
        h = cut[itype][jtype];
        ih = 1.0 / h;
	double wfd;
        if (domain->dimension == 3) {
          // Quintic spline
	  wfd = sph_dw_quintic3d(sqrt(rsq)*ih);
          wfd = wfd * ih * ih * ih * ih / sqrt(rsq);
        } else {
	  wfd = sph_dw_quintic2d(sqrt(rsq)*ih);
          wfd = wfd * ih * ih * ih / sqrt(rsq);
        }

        // compute pressure  of atom j with Tait EOS
	double pj = sph_pressure(B[jtype], rho0[jtype], gamma[itype], rbackground[jtype], rho[j]);
	fj = pj / (rho[j] * rho[j]);

        velx=vxtmp - v[j][0];
        vely=vytmp - v[j][1];
        velz=vztmp - v[j][2];

        // dot product of velocity delta and distance vector
        delVdotDelR = delx * velx + dely * vely + delz * velz;

        // Multiphase Viscosity (Multiphase, 1996)
        fvisc = 2 * viscosity[itype][jtype] / (rho[i] * rho[j]);

        fvisc *= imass * jmass * wfd;

        // total pair force & thermal energy increment
        fpair = - (imass*imass*fi + jmass*jmass*fj) * wfd;

        f[i][0] += delx * fpair + velx * fvisc;
        f[i][1] += dely * fpair + vely * fvisc;
        f[i][2] += delz * fpair + velz * fvisc;

        if (newton_pair || j < nlocal) {
          f[j][0] -= delx * fpair + velx * fvisc;
          f[j][1] -= dely * fpair + vely * fvisc;
          f[j][2] -= delz * fpair + velz * fvisc;
        }

        if (evflag)
          ev_tally(i, j, nlocal, newton_pair, 0.0, 0.0, fpair, delx, dely, delz);
      }
    }
  }

  if (vflag_fdotr) virial_fdotr_compute();
}

/* ----------------------------------------------------------------------
 allocate all arrays
 ------------------------------------------------------------------------- */

void PairSPHTaitwaterMultiphase::allocate() {
  allocated = 1;
  int n = atom->ntypes;

  memory->create(setflag, n + 1, n + 1, "pair:setflag");
  for (int i = 1; i <= n; i++)
    for (int j = i; j <= n; j++)
      setflag[i][j] = 0;

  memory->create(cutsq, n + 1, n + 1, "pair:cutsq");

  memory->create(rho0, n + 1, "pair:rho0");
  memory->create(soundspeed, n + 1, "pair:soundspeed");
  memory->create(gamma, n + 1, "pair:gamma");
  memory->create(rbackground, n + 1, "pair:rbackground");
  memory->create(B, n + 1, "pair:B");
  memory->create(cut, n + 1, n + 1, "pair:cut");
  memory->create(viscosity, n + 1, n + 1, "pair:viscosity");
}

/* ----------------------------------------------------------------------
 global settings
 ------------------------------------------------------------------------- */

void PairSPHTaitwaterMultiphase::settings(int narg, char **arg) {
  if (narg != 0)
    error->all(FLERR,
        "Illegal number of setting arguments for pair_style sph/taitwater/morris");
}

/* ----------------------------------------------------------------------
 set coeffs for one or more type pairs
 ------------------------------------------------------------------------- */

void PairSPHTaitwaterMultiphase::coeff(int narg, char **arg) {
  if (narg != 8)
    error->all(FLERR,
        "Incorrect args for pair_style sph/taitwater/morris coefficients (expect 5 or 6)");
  if (!allocated)
    allocate();

  int ilo, ihi, jlo, jhi;
  force->bounds(arg[0], atom->ntypes, ilo, ihi);
  force->bounds(arg[1], atom->ntypes, jlo, jhi);

  double rho0_one = force->numeric(arg[2]);
  double soundspeed_one = force->numeric(arg[3]);
  double viscosity_one = force->numeric(arg[4]);
  double gamma_one = force->numeric(arg[5]);
  double cut_one = force->numeric(arg[6]);
  double B_one = soundspeed_one * soundspeed_one  * rho0_one / gamma_one;
  double rbackground_one = force->numeric(arg[7]);

  int count = 0;
  for (int i = ilo; i <= ihi; i++) {
    rho0[i] = rho0_one;
    gamma[i] = gamma_one;
    soundspeed[i] = soundspeed_one;
    B[i] = B_one;
    rbackground[i] = rbackground_one;
    for (int j = MAX(jlo,i); j <= jhi; j++) {
      viscosity[i][j] = viscosity_one;
      cut[i][j] = cut_one;

      setflag[i][j] = 1;
      count++;
    }
  }

  if (count == 0)
    error->all(FLERR,"Incorrect args for pair coefficients");
}

/* ----------------------------------------------------------------------
 init for one type pair i,j and corresponding j,i
 ------------------------------------------------------------------------- */

double PairSPHTaitwaterMultiphase::init_one(int i, int j) {

  if (setflag[i][j] == 0) {
    error->all(FLERR,"Not all pair sph/taitwater/morris coeffs are not set");
  }

  cut[j][i] = cut[i][j];
  viscosity[j][i] = viscosity[i][j];
  return cut[i][j];
}

/* ---------------------------------------------------------------------- */

double PairSPHTaitwaterMultiphase::single(int i, int j, int itype, int jtype,
    double rsq, double factor_coul, double factor_lj, double &fforce) {
  fforce = 0.0;

  return 0.0;
}

double LAMMPS_NS::sph_pressure(double B, double rho0, double gamma, double rbackground, double rho) {
  double P = B*(pow(rho/rho0, gamma) - rbackground);
  return P;
}
