#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <math.h>
#include <string.h>
#include "mpi.h"


#define ROOT 0
#define TAG_ARR_DATA 0
#define TAG_NR_TRUES 1
#define TAG_HALT_JOB 2

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

	// between sin(210) and sin(330) is below -0.5, which is also equal to sin(7/4 * M_PI) and sin(11/6 * M_PI)
	// In trigonometer, if xd is over 2 M_PI, we subtract till xd is below 2 M_PI 
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
		A[i] = (rand() % N); // Each value is between 0 and 500
	}
}

void fill_ascending(int *A, int N) {
  for (int i = 0; i < N; i++) {
    A[i] = i;
  }
}

/* Work done by 1 processor */
void sequential(char *fill_type, char *work_type, FILE *fp) {
	int *arr = allocate_mem(N);
	if (strcmp(fill_type, "asc") == 0) {
		fill_ascending(arr, N); 	
	} else if (strcmp(fill_type, "rand") == 0) {
		fill_random(arr, N);
	} else {
		printf("Wrong filling for the array.\n"); exit(1);
	}
	
	int nr_true = 0;
	int result = 0;
	double time = -MPI_Wtime(); // This command helps us measure time. 
	for (int i = 0; i < N; i++) {
		if (work_type == "imbalanced") result = test(arr[i]);
		else result = test_imbalanced(arr[i]);

		if (result) {
			nr_true++;
			if (nr_true >= 100) break; 
		}		
	}

  	time += MPI_Wtime();
  	fprintf(fp, "Time spent for the sequential version: %f\n", time);
}


// Used for imbalanced test.
void do_job(int job_per_proc, int *sub_arr) {
	int nr_true = 0;
	int halt_job = 0;
	for (int i = 0; i < job_per_proc; i++) {

		// ** Implicit recieving: check if number of trues exceeds 100 in total.
		int flag = 0;
		MPI_Status status;
		MPI_Iprobe(ROOT, TAG_HALT_JOB, MPI_COMM_WORLD, &flag, &status); 
		while (flag) {
			MPI_Recv(&halt_job, 1, MPI_INT, ROOT, TAG_HALT_JOB, MPI_COMM_WORLD, MPI_STATUS_IGNORE); // blocking recv parameter
			if (halt_job) return;
			MPI_Iprobe(ROOT, TAG_HALT_JOB, MPI_COMM_WORLD, &flag, &status); // check for more updates
		}

		int result = test_imbalanced(sub_arr[i]);
		if (result) {
			nr_true++;
			int magic_number = 1;
			MPI_Send(&magic_number, 1, MPI_INT, ROOT, TAG_NR_TRUES, MPI_COMM_WORLD);  
			if (nr_true >= 100) return;
		}
	}
}


/* test_imbalanced is tested with multiple processors */
void imbalanced_parallel_work(int nr_procs, int proc_id, char* work_type, FILE *fp) {
	int job_per_proc = (N / (nr_procs));
	int *sub_arr = allocate_mem(job_per_proc);
	int *arr = NULL; 
	int halt_job = 1;

	if (proc_id == ROOT) { 			// Root machine distributes work
		arr = allocate_mem(N);
		if (strcmp(work_type, "asc") == 0) {
			fill_ascending(arr, N); 	
		} else if (strcmp(work_type, "rand") == 0) {
			fill_random(arr, N);
		} else {
			fprintf(stderr, "Wrong filling for the array.\n"); exit(1);
		}
	}
	// Scatter the random numbers from the root process to all processes in the MPI world
  	MPI_Scatter(arr, job_per_proc, MPI_INT, sub_arr, job_per_proc, MPI_INT, ROOT, MPI_COMM_WORLD);

	double time = -MPI_Wtime(); // This command helps us measure time. 
	if (proc_id == ROOT) { 					
		int total_nr_true = 0; 
		int i = 0;
		while (total_nr_true < 100 && i < job_per_proc) {
			// Computation that the root process does
			int result = test_imbalanced(sub_arr[i]); i++;
			if (result) {
				total_nr_true++;
				if (total_nr_true >= 100) {
					for (int id = 1; id < nr_procs; id++) {
						MPI_Send(&halt_job, 1, MPI_INT, id, TAG_HALT_JOB, MPI_COMM_WORLD);
					}
					break; // Break early to not recieve messages sent from other procs
				}
			}
			// Check if there is a message from another process to update the nr_trues.
			int flag = 0;
			MPI_Status status;
			MPI_Iprobe(MPI_ANY_SOURCE, TAG_NR_TRUES, MPI_COMM_WORLD, &flag, &status); // ** Implicit recieving, check for pending message, don't wait for it then it will increase the time
			// Non-blocking recv
			while (flag) {
				int other_true = 0;
				MPI_Recv(&other_true, 1, MPI_INT, MPI_ANY_SOURCE, TAG_NR_TRUES, MPI_COMM_WORLD, MPI_STATUS_IGNORE); 
				total_nr_true++;
				if (total_nr_true >= 100) { // If we always recieve a message and can't get out of this loop, we return ASAP
					for (int id = 1; id < nr_procs; id++) {
						MPI_Send(&halt_job, 1, MPI_INT, id, TAG_HALT_JOB, MPI_COMM_WORLD);
					}
					break; // Break early to not recieve messages sent from other procs
				}
				MPI_Iprobe(MPI_ANY_SOURCE, TAG_NR_TRUES, MPI_COMM_WORLD, &flag, &status); // check for more updates
			}
		}
	}
	else { 
		do_job(job_per_proc, sub_arr);
	}
	time += MPI_Wtime();
	fprintf(fp, "Process %d finished the job in %f seconds\n", proc_id, time);
}




/*
	- To reduce the communication overhead, we will reserve (N / nr_procs) job per processor
	- Make sure that the program keeps track of how many successful elements (number of trues) all processes have found together, 
	so that all processes can terminate as fast as possible (When nr_trues >= 100). 
	- Root machine uses async non-blocking send 
*/
double fixed_parallel_work(int nr_procs, int proc_id, char* work_type, FILE *fp) {
	int job_per_proc = (N / (nr_procs));
	int *sub_arr = allocate_mem(job_per_proc);
	int *arr = NULL; 
	int global_nr_true = 0;
	double global_time = 0;

	if (proc_id == ROOT) { 			// Root machine distributes work
		arr = allocate_mem(N);
		if (strcmp(work_type, "asc") == 0) {
			fill_ascending(arr, N); 	
		} else if (strcmp(work_type, "rand") == 0) {
			fill_random(arr, N);
		} else {
			fprintf(stderr, "Wrong filling for the array.\n"); exit(1);
		}
	}
	// Scatter the random numbers from the root process to all processes in the MPI world
  	MPI_Scatter(arr, job_per_proc, MPI_INT, sub_arr, job_per_proc, MPI_INT, ROOT, MPI_COMM_WORLD);
	
	double time = -MPI_Wtime(); // This command helps us measure time. 
	int local_nr_true = 0;
	for (int i = 0; i < job_per_proc; i++) {
		int result = test(sub_arr[i]);
		if (result) local_nr_true++;
		MPI_Allreduce(&local_nr_true, &global_nr_true, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
		if (global_nr_true >= 100) break;
	}
	time += MPI_Wtime();
	MPI_Barrier(MPI_COMM_WORLD);
	MPI_Reduce(&time, &global_time, 1, MPI_DOUBLE, MPI_MAX, ROOT, MPI_COMM_WORLD);
	return global_time;
}


int main(int argc, char *argv[]) {
	if (argc != 4) {
		fprintf(stderr, "arg[1] = rand or asc, arg[2] = file_name.txt, arg[3] = imbalanced or fixed\n"); exit(1);
	} 
	char *arr_filling = argv[1]; // rand or asc
	char *file_name = argv[2]; // file_name.txt
	char *work_type = argv[3]; // imbalanced or fixed

	// Initialize MPI env
	MPI_Init(&argc, &argv); 						
	int nr_procs = 0;
	int proc_id = -1;
	MPI_Comm_size(MPI_COMM_WORLD, &nr_procs); 		// Get number of processors we are gonna use for the job
    MPI_Comm_rank(MPI_COMM_WORLD, &proc_id); 		// Get rank (id) of processors
	
	FILE *fp = NULL;
	fp = fopen(file_name, "a");
	if (fp == NULL) {
		printf("Creating file %s\n", file_name);
		fp = fopen(file_name, "w+"); 
	}

	if (work_type == "fixed") {
		if (nr_procs > 1) {
			double time = fixed_parallel_work(nr_procs, proc_id, arr_filling, fp);
			if (proc_id == ROOT) fprintf(fp, "Time took to complete %s work with %d processors: %f\n", arr_filling, nr_procs, time);
		} 
		else {
			sequential(arr_filling, work_type, fp);
			fprintf(fp, "\n"); 
		}
	} 
	else if (work_type == "imbalanced") {
		if (nr_procs > 1) {
			imbalanced_parallel_work(nr_procs, proc_id, arr_filling, fp);
			// if (proc_id == ROOT) fprintf(fp, "Time took to complete %s work with %d processors: %f\n", arr_filling, nr_procs, time);
		} 
		else {
			sequential(arr_filling, work_type, fp);
			fprintf(fp, "\n"); 
		}
	}
	

	fclose(fp);
	
	MPI_Barrier(MPI_COMM_WORLD);
	MPI_Finalize(); 
	
	return 0;
}
