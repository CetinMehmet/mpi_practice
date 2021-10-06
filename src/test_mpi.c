#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <math.h>
#include "mpi.h"
#include <string.h>

#define ROOT 0
#define TAG_ARR_DATA 0
#define TAG_NR_TRUES 1

int halt_jobs = 0;
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

void sequential(char *work_type) {
	int *arr = allocate_mem(N);
	if (strcmp(work_type, "asc") == 0) {
		fill_ascending(arr, N); 	
	} else if (strcmp(work_type, "rand") == 0) {
		fill_random(arr, N);
	} else {
		printf("Wrong filling for the array.\n"); exit(1);
	}
	
	int nr_true = 0;
	double time = -MPI_Wtime(); // This command helps us measure time. 
	for (int i = 0; i < N; i++) {
		int result = test(arr[i]);
		if (result) {
			nr_true++;
		}
		if (nr_true >= 100) {
			break; 
		}
	}

  	time += MPI_Wtime();
  	printf("Time spent for the sequential version: %f\n", time);
}

/* Might be required in the future for point-to-point work distribution
void get_subset(int *arr, int* sub_arr, int startPoint, int jobs_per_proc) {
	int j = 0;
	for(int i = startPoint; i < (startPoint + jobs_per_proc); i++) {
		sub_arr[j] = arr[i];
		j++;
	}
}
*/

void do_job(int job_per_proc, int *sub_arr) {
	int nr_true = 0;
	for (int i = 0; i < job_per_proc; i++) {
		if (halt_jobs) {
			return;
		}
		if (nr_true >= 100) {
			return; // ** Stop computation immediatly after reaching 100 trues
		}

		int result = test(sub_arr[i]);
		if (result) {
			nr_true++;
			MPI_Send(&nr_true, 1, MPI_INT, ROOT, TAG_NR_TRUES, MPI_COMM_WORLD);  
		}
	}
}

/*
	- To reduce the communication overhead, we will reserve (N / nr_procs) job per processor
	- Make sure that the program keeps track of how many successful elements (number of trues) all processes have found together, 
	so that all processes can terminate as fast as possible (When nr_trues >= 100). 
	- Root machine uses async non-blocking send 
*/
void parallel_work(int nr_procs, int proc_id, char* work_type) {

	printf("Parallel program for processor %d has started!\n", proc_id);
	int job_per_proc = (N / (nr_procs));
	int *sub_arr = allocate_mem(job_per_proc);
	int *arr = NULL; 

	if (proc_id == ROOT) { 			// Root machine distributes work
		arr = allocate_mem(N);
		if (strcmp(work_type, "asc") == 0) {
			fill_ascending(arr, N); 	
		} else if (strcmp(work_type, "rand") == 0) {
			fill_random(arr, N);
		} else {
			printf("Wrong filling for the array.\n"); exit(1);
		}
	}
	// Scatter the random numbers from the root process to all processes in the MPI world
  	MPI_Scatter(arr, job_per_proc, MPI_INT, sub_arr, job_per_proc, MPI_INT, ROOT, MPI_COMM_WORLD);

	if (proc_id == ROOT) { 			
		double time_root = -MPI_Wtime(); // This command helps us measure time. 
		int total_nr_true = 0; 
		int i = 0;
		while (total_nr_true < 100 && i < job_per_proc) {
			int curr_true = 0;
			// Computation that the root process does
			int result = test(sub_arr[i]);
			i++;
			if (result) {
				curr_true++;
				if (total_nr_true >= 100) {
					break;
				}
			}
			// Check if there is a message from another process to update the nr_trues.
			int flag = 0;
			MPI_Status status;
			MPI_Iprobe(MPI_ANY_SOURCE, TAG_NR_TRUES, MPI_COMM_WORLD, &flag, &status); // ** Implicit recieving
			// If there is a message from any process, we know it's a true with the quantity of 1.
			while (flag) {
				int other_true = 0;
				MPI_Recv(&other_true, 1, MPI_INT, MPI_ANY_SOURCE, TAG_NR_TRUES, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
				total_nr_true = curr_true + other_true;
				if (total_nr_true >= 100) { // If we always recieve a message and can't get out of this loop, we return ASAP
					break;
				}
				MPI_Iprobe(MPI_ANY_SOURCE, TAG_NR_TRUES, MPI_COMM_WORLD, &flag, &status); // check for more updates
			}
		}
		halt_jobs = 1;
		time_root += MPI_Wtime(); // This command helps us measure time. 
		printf("Process %d finished the job in %f seconds\n", proc_id, time_root); 
		return;
	}
	else { 
		double time = -MPI_Wtime(); // This command helps us measure time. 
		do_job(job_per_proc, sub_arr);
		time += MPI_Wtime();
		printf("Process %d finished the job in %f seconds\n", proc_id, time);
		return;
	}
}


int main(int argc, char *argv[]) {
	// Initialize MPI env
	MPI_Init(&argc, &argv); 						
	int nr_procs = 0;
	int proc_id = -1;
	MPI_Comm_size(MPI_COMM_WORLD, &nr_procs); 		// Get number of processors we are gonna use for the job
    MPI_Comm_rank(MPI_COMM_WORLD, &proc_id); 		// Get rank (id) of processors
	
	if (proc_id == ROOT) {
		printf("Testing program with %d processors\n", nr_procs);
	}

	printf("Ascending parallel work started!\n");
	parallel_work(nr_procs, proc_id, "asc");

	printf("Random parallel work started!\n");
	parallel_work(nr_procs, proc_id, "rand");

	printf("Ascending Sequential work started!\n");
	sequential("asc");

	printf("Random Sequential work started!\n");
	sequential("rand");

	MPI_Finalize(); // Finalize MPI env  	
}
