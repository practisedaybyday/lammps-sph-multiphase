#! /bin/bash

set -e
set -u
Lx=1.0
nx=40
ndim=3
np=8
D_heat_d=1.0
D_heat_g=1.0
sph_eta_d=0.69395
cv_d=1.0
cv_g=2.0
dT=0.0
Hwv=16.0
alpha=200
dprob=0.01
sph_rho_d=0.1
time_k=1.0
# parameters for kana
lmp=../../../../src/lmp_linux
mpirun=mpirun
proc="-np ${np}"

dname=data-nx${nx}-ndim${ndim}-Lx${Lx}-D_heat_d${D_heat_d}-alpha${alpha}\
-Hwv${Hwv}-dprob${dprob}-time_k${time_k}-cv_d${cv_d}-sph_rho_d${sph_rho_d}-dT${dT}\
-cv_g${cv_g}-D_heat_g${D_heat_g}-sph_eta_d${sph_eta_d}

rm -rf ${dname}
mkdir -p ${dname}
${mpirun} ${proc} ${lmp} -in insert.lmp \
    -var alpha ${alpha} -var D_heat_d ${D_heat_d} -var ndim ${ndim} -var nx ${nx} \
    -var Hwv ${Hwv} -var dprob ${dprob} -var time_k ${time_k} \
    -var Lx ${Lx} -var cv_d ${cv_d} -var sph_rho_d ${sph_rho_d} -var dT ${dT} \
    -var cv_g ${cv_g} -var D_heat_g ${D_heat_g} \
    -var dname ${dname} -var sph_eta_d ${sph_eta_d}
