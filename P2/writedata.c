#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>

/* 
 * Writes exponential data xe^y into grid. 
 * 
 * IMPORTANT: 
 * User types in dimensions in MATRIX NOTATION: Y by X. 
 * However, computations in main.c will use cartesian
 * notation: (X,Y). 
 */

int main(int argc, char* argv[]) {
	if(argc!=5) {
		printf("USAGE INFO:\n\t./writedata [Y Range] [X Range] [# points y] [# points x]\n");
		exit(0);
	}

	int x_dim = atoi(argv[4]);
	int y_dim = atoi(argv[3]);
	double x_range, y_range;
	sscanf(argv[1], "%lf", &y_range);
	sscanf(argv[2], "%lf", &x_range);
	printf("x_range = %lf, y_range = %lf\n", x_range, y_range);
	char * filename = malloc(20);
	sprintf(filename, "Input_Data/data_%dx%d.txt", y_dim, x_dim);
	printf("x_dim=%d\ny_dim=%d\nWriting data to %s\n",x_dim, y_dim, filename);
	int fd = open(filename, O_WRONLY | O_CREAT, 0644, 1);


	// Write dimensions & range metadata into file
	write(fd, &x_dim, sizeof(int));
	write(fd, &y_dim, sizeof(int));
	write(fd, &x_range, sizeof(double));
	write(fd, &y_range, sizeof(double));


	// Write data into file
	int counter = 0, x_counter = 0, stop = x_dim*y_dim;
	double e = 2.718281828, xval, yval, val, xinc, yinc; 
	xinc = x_range/(double)x_dim;
	yinc = y_range/(double)y_dim;
	printf("x_inc = %lf, y_inc = %lf\n", xinc, yinc);
	for(yval=0; counter<stop; yval+=yinc) {
		for(xval=0; x_counter<x_dim; xval+=xinc) {
			val = xval*pow(e, yval);
			//if(xval==0)
			//	printf("\n\n");
			//printf("writing @(%lf, %lf)=(%d, %d):   %lf\n", xval, yval, counter%x_dim, counter/x_dim, val);
			write(fd, &val, sizeof(double));
			counter++;
			x_counter++;
		}
		x_counter = 0;
	}
	close(fd);
}

