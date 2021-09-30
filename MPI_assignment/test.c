#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <math.h>
#include "mpi.h"

#define ROOT 1

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
	int *arr = allocate_mem(N);
	fill_random(arr, N);
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

void get_subset(int *arr, int* sub_arr, int startPoint, int jobs_per_proc) {
	for(i = startPoint, j = 0; i < (startPoint + jobs_per_proc), j < jobs_per_proc; i++, j++)
   		sub_arr[j] = arr[i];
}

/*
	- To reduce the communication overhead, we will reserve (N / nr_procs) job per processor
	- Make sure that the program keeps track of how many successful elements (number of trues) all processes have found together, 
	so that all processes can terminate as fast as possible (When nr_trues >= 100). 
*/
void parallel_work(int nr_procs, int proc_id, int job_per_proc) {
	printf("Parallel program for processor %d has started!\n", proc_id);
	int nr_true = 0;

	if (proc_id == ROOT) { 			// Root machine: only distributes work
		int *arr = allocate_mem(N);
		int* sub_arr = allocate_mem(job_per_proc);
		fill_ascending(arr, N);
		for (int id = 1; i < nr_procs; i++) {
			get_subset(arr, sub_arr, (id-1) * job_per_proc, job_per_proc);
			MPI_Send(sub_arr, job_per_proc, MPI_INT, id, 0, MPI_COMM_WORLD);
			printf("Process 0 sent data %d to process 1\n", i, N);
		}
		
		// TODO: gather all nr_trues and check if more than 100
  	} 
	else {
		sub_arr = allocate_mem(job_per_proc); // allocate sufficient size to buffer 
    	MPI_Recv(sub_arr, job_per_proc, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE); // Every worker recieves work from root machine
		printf("Process %d received data %d from process 0\n", proc_id, N);
		for (int i = 0; i < job_per_proc; i++) {
			int result = test(arr[i]);
			if (result) {
				nr_true++;
			}
		}
  	} 	
}


int main(int argc, char *argv[]) {
	// Initialize MPI env
	MPI_Init(&argc, &argv); 						
	int nr_procs = 0;
	int proc_id = -1;
	int name_len = 0; 
	char proc_names[MPI_MAX_PROCESSOR_NAME];
	MPI_Comm_size(MPI_COMM_WORLD, &nr_procs); 		// Get number of processors we are gonna use for the job
    MPI_Comm_rank(MPI_COMM_WORLD, &proc_id); 		// Get rank (id) of processors
	MPI_Get_processor_name(proc_names, &name_len); 	// Get current processor name

	clock_t begin = clock();

	int job_per_proc = (N / (nr_procs - 1));

	parallel_work(nr_procs, proc_id, job_per_proc);
	
	clock_t end = clock();
  	double time_spent = (double)(end - begin) / CLOCKS_PER_SEC;
  	printf("Time spent (seconds) for the paralell version with %d processors: %f\n", nr_procs, time_spent);
	
	MPI_Finalize(); // Finalize MPI env
  	
}
