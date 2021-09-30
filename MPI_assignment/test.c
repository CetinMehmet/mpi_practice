#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <math.h>
#include "mpi.h"

int N = 500;
int R = 100;

int test(int x) {
	// Transform to a number beween 0 and 2 * pi.
	double xd = ((double)x * (2 * M_PI) / N);

	// Do a fixed amount of computation.
	int n = 25000000;
	for (int i = 0; i < n; i++) {
		xd = sin(xd);
		xd = asin(xd);
	}
	xd = sin(xd);

	// Test the result.
	return xd < -0.5;
}

int test_imbalanced(int x) {
  // Transform to a number beween 0 and 2 * pi.
  double xd = ((double)x * (2 * M_PI) / N);

  // Do computation, the amount depends on the value of x.
  int n = 100000 * x;

  for (int i = 0; i < n; i++) {
    xd = sin(xd);
    xd = asin(xd);
  }
  xd = sin(xd);

  // Test the result.
  return xd < -0.5;
}

int *allocate_mem(int N) {
	int *A = calloc(N, sizeof(int));
	if (!A)
		exit(0);
	return A;
}

void fill_random(int *A, int N) {
	srand(time(0));

	for (int i = 0; i < N; i++) {
		A[i] = (rand() % N);
	}
}

void fill_ascending(int *A, int N) {
  for (int i = 0; i < N; i++) {
    A[i] = i;
  }
}

void sequential() {
	int *arr = init_random_arr();
	int nr_true = 0;
	
	printf("Sequential program has started!\n");
	clock_t begin = clock();

	for (int i = 0; i < N; i++) {
		int result = test(arr[i]);
		if (result) {
		nr_true++;
		}
	}

  	clock_t end = clock();
  	double time_spent = (double)(end - begin) / CLOCKS_PER_SEC;
  	printf("Time spent (seconds) for the sequential version: %f\n", time_spent);

  	printf("Number of trues for N=%d, R=%d: %d\n", N, R, nr_true);
}


int *init_random_arr() {
	int arr[N]; 
	arr = allocate_mem(N);
	fill_random(arr, N);
	return arr;
}

int *init_ascending_arr() {
	int arr[N]; 
	arr = allocate_mem(N);
	fill_ascending(arr, N);
	return arr;
}
/*
	- To reduce the communication overhead, we will reserve (N / nr_procs) job per processor
	- Make sure that the program keeps track of how many successful elements (number of trues) all processes have found together, 
	so that all processes can terminate as fast as possible (When nr_trues >= 100). 
*/
void parallel_work(int nr_procs, int proc_id, int job_per_proc) {
	printf("Parallel program for processor %d has started!\n", proc_id);
	if (proc_id == 0) { 			// Root processca
		int *arr = init_ascending_arr();
		int nr_true = 0;
		fill_random(arr, N);

		MPI_Send(arr, N, MPI_BYTE, 1, 0, MPI_COMM_WORLD);
		for (int i = 0; i < job_per_proc; i++) {
			int result = test(arr[i]);
			if (result) {
				nr_true++;
			}
		}
  	} 
	else if (proc_id == 1) {
		int nr_true = 0;
		int *arr;
    	MPI_Recv(&arr, N, MPI_BYTE, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
		printf("Process 1 received data %d from process 0\n", N);
		for (int i = job_per_proc; i < N; i++) {
			int result = test(arr[i]);
			if (result) {
				nr_true++;
			}
		}
  	} 	
	
  	printf("Number of trues for N=%d, R=%d: %d\n", N, R, nr_true);
}

void initialize_mpi(int *nr_procs, int *proc_id, int *name_len, char **proc_names) {
	MPI_Comm_size(MPI_COMM_WORLD, &nr_procs); 		// Get number of processors we are gonna use for the job
    MPI_Comm_rank(MPI_COMM_WORLD, &proc_id); 		// Get rank (id) of processors
	MPI_Get_processor_name(proc_names, &name_len); 	// Get current processor name
}
int main(int argc, char *argv[]) {
	// Initialize MPI env
	MPI_Init(&argc, &argv); 						
	int nr_procs = 0;
	int proc_id = -1;
	int name_len = 0; 
	char proc_names[MPI_MAX_PROCESSOR_NAME];
	initialize_mpi(&nr_procs, &proc_id, &name_len, &proc_names);

	clock_t begin = clock();

	int job_per_proc = N / nr_procs;
	parallel_work(nr_procs, proc_id, job_per_proc);
	
	clock_t end = clock();
  	double time_spent = (double)(end - begin) / CLOCKS_PER_SEC;
  	printf("Time spent (seconds) for the paralell version with %d processors: %f\n", nr_procs, time_spent);
	
	MPI_Finalize(); // Finalize MPI env
  	
}
