#include "stdlib.h"
#include "stdio.h"
#include "string.h"
#include "math.h"

#define XRANGE 1
#define YRANGE 1

#define THRESH 1e-5

extern "C"
void VTK_out(const int N, const int M, const double *Xmin, const double *Xmax,
             const double *Ymin, const double *Ymax, const double *T,
             const int index);

__device__ int d_Px, d_Py;
__device__ long d_grid_size, d_internal_size;


__global__
void prepare_grids(double *T, double * T_tmp, double *S, double * errors, long grid_size, long internal_size, int Px, int Py) {
	long id = blockIdx.x*blockDim.x + threadIdx.x;
	long mapped_id = id-(2*(id/Px)-1)-(Px);
	if(id >= grid_size)
		return;
	double val = (id%Px)*((double)XRANGE/Px)*powf(2.718281828, (id/Px)*((double)YRANGE/Py));
	S[id] = val;
	if(id/Px==0 || id/Px==Py-1 || id%Px==0 || id%Px==Px-1) { 
		T[id] = val;
		T_tmp[id] = val;
		return;
	} else {
		T[id] = 0;
		T_tmp[id] = 0;
	}
	errors[mapped_id] = 0;
}


__global__
void update_temporary(double * T, double * T_tmp, double * S, double * errors, long grid_size, int Px, int Py) {
	long id = blockIdx.x*blockDim.x + threadIdx.x;
	if(id>=grid_size || id/Px==0 || id/Px==Py-1 || id%Px==0 || id%Px==Px-1)
		return;
	T_tmp[id] = (-1*S[id]*powf((((double)XRANGE/Px)*((double)YRANGE/Py)),2)+(T[id-1]+T[id+1])*powf(((double)YRANGE/Py),2)+ \
	(T[id-Px]+T[id+Px])*powf(((double)XRANGE/Px),2))/(2*powf(((double)XRANGE/Px),2)+2*powf(((double)YRANGE/Py),2));
}

__global__
void update_real(double * T, double * T_tmp, double * S, double * errors, long grid_size, int Px, int Py) {
	long id = blockIdx.x*blockDim.x + threadIdx.x;
	if(id>=grid_size || id/Px==0 || id/Px==Py-1 || id%Px==0 || id%Px==Px-1)
		return;
	T[id] = T_tmp[id];
}


int main(int argc, char **argv) {
	if(argc!=3) {
		printf("Usage:\n\t./main [xdim] [ydim]\n");
		exit(0);
	}
	h_Px = atoi(argv[1]);
	h_Py = atoi(argv[2]);
	h_grid_size = h_Px*h_Py;
	h_internal_size = h_grid_size - 2*h_Px - 2*h_Py + 4;
	cudaMemcpy(&d_Px, &h_Px, sizeof(int), cudaMemcpyHostToDevice);
	cudaMemcpy(&d_Py, &h_Py, sizeof(int), cudaMemcpyHostToDevice);
	cudaMemcpy(&d_grid_size, &h_grid_size, sizeof(long), cudaMemcpyHostToDevice);
	cudaMemcpy(&d_internal_size, &h_internal_size, sizeof(long), cudaMemcpyHostToDevice);


	// Allocate grids in Host to recieve from GPU (pinned memory)
	double * h_S;
	double * h_T;
	double * h_T_tmp;
	cudaMallocHost((void**)&h_S, h_grid_size*sizeof(double));
	cudaMallocHost((void**)&h_T, h_grid_size*sizeof(double));
	cudaMallocHost((void**)&h_T_tmp, h_grid_size*sizeof(double));
	
	// Allocate grids in GPU memory
	double * d_S;
	double * d_T;
	double * d_T_tmp;
	double * d_errors;
	cudaMalloc((double**)&d_S, h_grid_size*sizeof(double));
	cudaMalloc((double**)&d_T, h_grid_size*sizeof(double));	
	cudaMalloc((double**)&d_T_tmp, h_grid_size*sizeof(double));
	cudaMalloc((double**)&d_errors, h_internal_size*sizeof(double));

	int blocks = (ceil((double)(Px*Py)/1000));
	int threadsperblock = 1000;
	printf("Running on %d blocks each with %d threads\n",blocks,threadsperblock);

	prepare_grids<<<blocks, threadsperblock>>>(d_T,d_T_tmp,d_S,d_errors,grid_size,d_internal_size,d_Px,d_Py);
	int iter = 0;
	while(iter < 100000) {
		update_temporary<<<blocks,threadsperblock>>>(d_T,d_T_tmp,d_S,d_errors,d_grid_size,d_Px,d_Py);
		cudaDeviceSynchronize();
		update_real<<<blocks,threadsperblock>>>(d_T,d_T_tmp,d_S,d_errors,d_grid_size,d_Px,d_Py);
		if(iter%1000==0) {

			printf("iter = %d\n", iter);
		}
		iter++;
	}
	printf("Finished\n");

	cudaMemcpy(h_T, d_T, h_grid_size*sizeof(double), cudaMemcpyDeviceToHost);

	// Output .vtk file for ParaView
	double Xmin = 0;
	double Ymin = 0;
	double Xmax = XRANGE;
	double Ymax = YRANGE;
	VTK_out(h_Px, h_Py, &Xmin, &Xmax, &Ymin, &Ymax, h_T, 0);

}
