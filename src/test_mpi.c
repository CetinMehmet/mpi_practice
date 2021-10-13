#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <math.h>
#include <string.h>
#include "mpi.h"


#define ROOT 0
#define TAG_JOB_DONE 0
#define TAG_NEW_JOB 1
#define TAG_HALT_PROC 2

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
double sequential(char *fill_type, char *work_type, FILE *fp) {
	int *arr = allocate_mem(N);
	if (strcmp(fill_type, "asc") == 0) {
		fill_ascending(arr, N); 	
	} else if (strcmp(fill_type, "rand") == 0) {
		fill_random(arr, N);
	} else {
		printf("Wrong filling for the array.\n"); exit(1);
	}
	
	int nr_true = 0, result = 0;
	double time = -MPI_Wtime(); // This command helps us measure time. 
	for (int i = 0; i < N; i++) {
		if (strcmp(work_type, "imbalanced") == 0) {
			result = test_imbalanced(arr[i]);
		} else {
			result = test(arr[i]);
		}

		if (result) {
			nr_true++;
			if (nr_true >= 100) break; 
		}		
	}

  	time += MPI_Wtime();
  	return time;
}


/* 1 master and n-1 worker nodes */ 
double imbalanced_parallel_work(int nr_procs, int proc_id, char* fill_type, FILE *fp) {
	printf("Imbalanced job started in process %d\n", proc_id);
	int *arr = NULL; 
	double time = 0.0;

	if (proc_id == ROOT) { 			
		arr = allocate_mem(N);
		if (strcmp(fill_type, "asc") == 0) {
		fill_ascending(arr, N); 	
		} else if (strcmp(fill_type, "rand") == 0) {
			fill_random(arr, N);
		} else {
			printf("Wrong filling for the array.\n"); exit(1);
		}

		// Initially send jobs to all worker procs
		int idx = 0; 
		for (idx = 0; idx < nr_procs-1; idx++) MPI_Send(&arr[idx], 1, MPI_INT, idx+1, TAG_NEW_JOB, MPI_COMM_WORLD);

		time = -MPI_Wtime();
		int total_nr_true = 0;
		while (total_nr_true <= 100) {

			int flag = 0;
			MPI_Status status;
			MPI_Iprobe(MPI_ANY_SOURCE, TAG_JOB_DONE, MPI_COMM_WORLD, &flag, &status); 
			while (flag) {
				int is_true = 0;
				MPI_Recv(&is_true, 1, MPI_INT, MPI_ANY_SOURCE, TAG_JOB_DONE, MPI_COMM_WORLD, &status); 
				if (is_true) {
					total_nr_true++;
					if (total_nr_true >= 100) {
						int halt_proc = 1;
						for (int i = 1; i < nr_procs; i++) 
							MPI_Send(&halt_proc, 1, MPI_INT, i, TAG_HALT_PROC, MPI_COMM_WORLD);	
					}
				}
				
				// Send a job to the process that completed the job and advance arr index
				MPI_Send(&arr[idx], 1, MPI_INT, status.MPI_SOURCE, TAG_NEW_JOB, MPI_COMM_WORLD);
				idx++; 

				// Check for more updates
				MPI_Iprobe(MPI_ANY_SOURCE, TAG_JOB_DONE, MPI_COMM_WORLD, &flag, &status); 
			}
		}
		time += MPI_Wtime();
	}
	
	else {
		int halt_proc = 0;
		while (halt_proc == 0) {
			int flag = 0, job = 0;
			MPI_Status status;
			MPI_Iprobe(ROOT, TAG_NEW_JOB, MPI_COMM_WORLD, &flag, &status); 
			while (flag) {
				MPI_Recv(&job, 1, MPI_INT, ROOT, TAG_NEW_JOB, MPI_COMM_WORLD, &status); 
				
				// Do job and request for a new job, and also send the result
				int is_true = test_imbalanced(job);
				MPI_Send(&is_true, 1, MPI_INT, ROOT, TAG_JOB_DONE, MPI_COMM_WORLD); 
				
				// Check for more updates
				MPI_Iprobe(ROOT, TAG_NEW_JOB, MPI_COMM_WORLD, &flag, &status); 
			}

			// Check if we need to update halt_proc 
			MPI_Iprobe(ROOT, TAG_HALT_PROC, MPI_COMM_WORLD, &flag, MPI_STATUS_IGNORE); 
			if (flag) MPI_Recv(&halt_proc, 1, MPI_INT, ROOT, TAG_HALT_PROC, MPI_COMM_WORLD, MPI_STATUS_IGNORE); 
		}
	}
	return time;
}


/* n worker nodes */
double fixed_parallel_work(int nr_procs, int proc_id, char* fill_type, FILE *fp) {
	int *arr = NULL; 
	if (proc_id == ROOT) { 		
		arr = allocate_mem(N);
		if (strcmp(fill_type, "asc") == 0) {
			fill_ascending(arr, N); 	
		} else if (strcmp(fill_type, "rand") == 0) {
			fill_random(arr, N);
		} else {
			printf("Wrong filling for the array.\n"); exit(1);
		}
	}

	// Scatter the random numbers from the root process to all processes in the MPI world
	int job_per_proc = (N / (nr_procs));
	int *sub_arr = allocate_mem(job_per_proc);
  	MPI_Scatter(arr, job_per_proc, MPI_INT, sub_arr, job_per_proc, MPI_INT, ROOT, MPI_COMM_WORLD);
	
	double global_time = 0;
	int global_nr_true = 0; 
	double time = -MPI_Wtime(); 
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

	if (strcmp(work_type, "fixed") == 0) {
		if (nr_procs > 1) {
			double time = fixed_parallel_work(nr_procs, proc_id, arr_filling, fp);
			if (proc_id == ROOT) 
				fprintf(fp, "Time took to complete %s fixed work with %d processors: %f\n", arr_filling, nr_procs, time);
		} 
		else {
			double time = sequential(arr_filling, work_type, fp);
			fprintf(fp, "Time took to complete %s %s work for sequential program: %f\n", arr_filling, work_type, time);
		}
	} 
	else if (strcmp(work_type, "imbalanced") == 0) {
		if (nr_procs > 1) {
			double time = imbalanced_parallel_work(nr_procs, proc_id, arr_filling, fp);
			if (proc_id == ROOT) 
				fprintf(fp, "Time took to complete %s imbalanced work with %d processors: %f\n", arr_filling, nr_procs-1, time);
		} 
		else {
			double time = sequential(arr_filling, work_type, fp);
			fprintf(fp, "Time took to complete %s %s work for sequential program: %f\n", arr_filling, work_type, time);
		}
	}

	fclose(fp);
	
	MPI_Barrier(MPI_COMM_WORLD);
	MPI_Finalize(); 
	
	return 0;
}
