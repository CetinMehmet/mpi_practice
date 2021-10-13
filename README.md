# Parallel Programming Assignment for the Programming Large Scale Parallel Systems course

## Running program in the DAS5 cluster

### To run to program in DAS, login to DAS:
`ssh <username>@fs0.das5.cs.vu.nl`

### Load required modules prun and mpicc:
`module load prun openmpi/gcc/64`

### To compile the program run:
`mpicc -Wall -O3 -o test_mpi test_mpi.c -lm`

### To run the compiled program:
`prun -np <nr_nodes> -<nr_cores> -script $PRUN_ETC/prun-openmpi ./test_mpi <arr_filling> <file_name> <work_type>`

### Running the program, example 1 (1 node, 4 cores, ascending filling, file name: asc_work_4.txt, fixed work):
`prun -np 1 -4 -script $PRUN_ETC/prun-openmpi ./test_mpi asc asc_work_4.txt fixed`

### Running the program, example 2 (2 nodes, 4 cores, random filling, file name: asc_work_4.txt, imbalanced work):
`prun -np 2 -4 -script $PRUN_ETC/prun-openmpi ./test_mpi rand rand_imb_work_8.txt imbalanced`

## Running program in local computer
Depends on what kind of OS you're using. So it's better to look this up online :)