#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <mpi.h>
#include <stdlib.h>
#include <math.h>
#include <sys/time.h>

#define MAX_PROCS 1000

int myrank, virtual_rank, np, per_proc;

/* Compute single function value. 
 * To swap function, modify this only. 
 */
double func_val(double x) {
	return 4/(1+pow(x,2));
}

/* Store function values in array. 
 * Only one special process will call
 * this function, and pass the chunks
 * to its friends. 
 */
void func_gen(double * arr, double X_min, double X_max, int Points) {
	double x = X_min, Delta = (X_max-X_min)/Points;
	for(int i=0; i<Points; i++, x += Delta)
		arr[i] = func_val(x);
}

/* Takes as input an array of size count and 
 * an empty array myshare with size per_proc. 
 * Will copy its share into myshare (if this
 * is a leaf-reaching propagate), and free 
 * memory of arr. It will also propagate the
 * remaining data to the right of its share to 
 * the appropriate process down the binary tree.
 * Proactively halves count for next propagation.  
 * 
 * RETURN VALUE: 0 if finished reached leaf node
 * process. 1 if further propagations needed. 
 */
int propagate(double *arr, int *count, double *myshare) {
	// Reached leaf-node process. copy data to myshare
	if(*count == per_proc) {
		memcpy(myshare, arr, per_proc*sizeof(double));
		free(arr);
		return 0;
	}

	// Calculate partitions and destinations
	int chunks = *count/per_proc;
	int send_chunks = chunks>>1;
	int dest = myrank + send_chunks;

	// Allocate send array
	int send_sz = per_proc*send_chunks;
	double * send_arr = malloc(send_sz*sizeof(double));

	// Copy from array to send arrays
	memcpy(send_arr, &arr[*count-send_sz], send_sz*sizeof(double));

	// Send work to next node down the tree
	MPI_Send(send_arr, send_sz, MPI_DOUBLE, dest, 0, MPI_COMM_WORLD);
	
	// Free send_array (arr still needed for next propagate)
	free(send_arr);

	*count >>= 1;
	return 1;
}

int main(int argc, char* argv[]) {
	MPI_Status status;
	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &myrank);
	MPI_Comm_size(MPI_COMM_WORLD, &np);
	struct timeval stop, start;

	// Parse arguments
	if(virtual_rank==0 && argc!=4) {
		printf("Abort. main needs 3 arguments: \"mpirun -np [# procs] main [# Points] [X_min] [X_max]\"\n");
		MPI_Abort(MPI_COMM_WORLD, 16);
	}
	MPI_Barrier(MPI_COMM_WORLD);
	int Points = strtol(argv[1], NULL, 10);
	double X_min = atof(argv[2]);
	double X_max = atof(argv[3]);

	// Check divisibility of labor
	if(virtual_rank==0 && (Points % np != 0 || Points == np) ) {
		printf("Abort. Number of processes (%d) must divide number of points (%d), and " \
		"cannot equal number of points.\n", np, Points);
		MPI_Abort(MPI_COMM_WORLD, 17);
	}
	MPI_Barrier(MPI_COMM_WORLD);
	per_proc = Points/np;

	/* 
	 * Each proc gets a virtual_rank: an offset from the closest
	 * power of 2 rank to its left
	 * Mask starts at -1 = 0xFFFFFFFF
	 */
	int offset=0, mask=-1, end;
	while( myrank<(np&mask) ) {
		end = mask;
		mask<<=1;
	}
	offset = np&mask;
	end &= np;
	virtual_rank = myrank - offset;
	int virtual_chunks = end - offset;
	int num_virtual = 1;

	// Allocate arrays for receiving
	double * arr;
	int arr_sz=0;
	double * myshare = malloc(per_proc*sizeof(double));

	/* If I am rank 0, I will first distribute assignments
	 * to all virtual_rank 0, then propagate my remaining
	 * data to my virtual tree. Else, I will wait to receive
	 * my assignment and propagate it down the my virtual
	 * tree.  
	 */
	if(myrank==0) {
		double * snd_bf;
		int msk_0 = 1 << (sizeof(int)*8-1);
		int msk_1=msk_0, prev_offset=0, snd_sz;
		offset = 0;
		arr = malloc(Points*sizeof(double));
		func_gen(arr, X_min, X_max, Points);
		gettimeofday(&start, NULL);	// Start timer AFTER you compute function
		for(int i=0; i<sizeof(int)*8; i++) {
			prev_offset = offset;
			offset = msk_0&np;
			snd_sz = per_proc*(offset-prev_offset);
			msk_0 >>= 1; msk_0 |= msk_1;
			if(snd_sz==0) continue;
			if(prev_offset != 0) {
				num_virtual++;
				snd_bf = malloc(snd_sz*sizeof(double));
				memcpy(snd_bf, arr+(prev_offset*per_proc), snd_sz*sizeof(double));
				MPI_Send(snd_bf, snd_sz, MPI_DOUBLE, prev_offset, 7, MPI_COMM_WORLD);
			}
		}
		arr_sz = virtual_chunks*per_proc;
		while(propagate(arr, &arr_sz, myshare)==1);
	} else {
		// Block until you receive a message, then receive and propagate down tree
		MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
		MPI_Get_count(&status, MPI_DOUBLE, &arr_sz);
		MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
		MPI_Get_count(&status, MPI_DOUBLE, &arr_sz);
		arr = malloc(arr_sz*sizeof(double));
		MPI_Recv(arr, arr_sz, MPI_DOUBLE, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
		while(propagate(arr, &arr_sz, myshare)==1);
	}

	// Perform the integration
	double sum=0;
	double Delta = (X_max-X_min)/Points;
	for(int i=0; i<per_proc-1; i++)
		sum += (myshare[i]+myshare[i+1])*Delta/2;
	free(myshare);

	// Gather all sums into the procs with virtual rank 0
	double recv_sum=0;
	int rank_decay = virtual_rank;
	for(int np_grow=2; np_grow<=virtual_chunks; np_grow<<=1, rank_decay>>=1) {
		if(rank_decay % 2 == 1) {
			MPI_Send(&sum, 1, MPI_DOUBLE, myrank-(np_grow>>1), 5, MPI_COMM_WORLD);
			break; // Whoever you sent to now represents your group. you leave. 
		}
		else if(rank_decay % 2 == 0) {
			MPI_Recv(&recv_sum, 1, MPI_DOUBLE, myrank+(np_grow>>1), 5, MPI_COMM_WORLD, &status);
			sum += recv_sum;
		}
	}

	// Gather all virtual rank 0 sub-integrals into rank 0
	if(virtual_rank==0 && myrank!=0)
		MPI_Send(&sum, 1, MPI_DOUBLE, 0, 9, MPI_COMM_WORLD);

	if(myrank==0) {
		double t_sum=0;
		for(int i=0; i<num_virtual-1; i++) {
			MPI_Recv(&t_sum, 1, MPI_DOUBLE, MPI_ANY_SOURCE, 9, MPI_COMM_WORLD, &status);
			sum += t_sum;
		}
	}

	// Display results
	if(myrank==0) {
		gettimeofday(&stop, NULL);
		printf("\nPROC %d REPORTS SUM = %lf\t <-- Result. ELAPSED TIME: %f sec\n", myrank, sum, (double)(stop.tv_usec-start.tv_usec)/1000000 + stop.tv_sec-start.tv_sec);
	}


	MPI_Finalize();
	return 0;
}
