#! /bin/bash

nx=20
ndim=2
np=1
dname=data-nx${nx}-ndim${ndim}-np${np}a
rm -rf ${dname}
mkdir -p ${dname}
mpirun -np ${np}  ../../../../src/lmp_linux -in insert.lmp -var ndim ${ndim} -var nx ${nx} -var dname ${dname}

