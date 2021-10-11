#!/bin/bash

for VAR in 1 2 3 4 5
do
    prun -np 1 -1 -script $PRUN_ETC/prun-openmpi ./test_mpi asc asc_work_1.txt
    prun -np 1 -2 -script $PRUN_ETC/prun-openmpi ./test_mpi asc asc_work_2.txt
    prun -np 1 -4 -script $PRUN_ETC/prun-openmpi ./test_mpi asc asc_work_4.txt
    prun -np 1 -8 -script $PRUN_ETC/prun-openmpi ./test_mpi asc asc_work_8.txt
done
