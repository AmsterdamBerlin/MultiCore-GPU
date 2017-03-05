#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

__device__ int iterate_pixel(float x, float y, float c_re, float c_im)
{
	int c=0;
	float z_re=x;
	float z_im=y;
	while (c<255) {
		float re2=z_re*z_re;
		float im2=z_im*z_im;
		if ((re2+im2) > 4) break; 
		z_im=2*z_re*z_im + c_im;
		z_re=re2-im2 + c_re;
		c++;
	}
	return c;
}

__global__ void calc_fractal(int width, int height, float c_re, float c_im, unsigned char* dest)
{	
	int x;	
	int y;

 	x = blockIdx.x * blockDim.x + threadIdx.x ;
 	y = blockIdx.y * blockDim.y + threadIdx.y;

	float f_x=(float)(x*0.8)/(float)(width)-0.8;
	float f_y=(float)(y*0.8)/(float)(height)-0.8;

	dest[y*width+x]=iterate_pixel(f_x,f_y,c_re,c_im);

}

/*void calc_fractal(int width, int height, float c_re, float c_im, unsigned char* dest)
{
	int x;
	int y;
	for (y=0;y<height;y++)
		for (x=0;x<width;x++)
		{
			float f_x=(float)(x*0.8)/(float)(width)-0.8;
			float f_y=(float)(y*0.8)/(float)(height)-0.8;
			dest[x+y*width]=iterate_pixel(f_x,f_y,c_re,c_im);
		}
}*/

// Write a width by height 8-bit color image into File "filename"
void write_ppm(unsigned char* data,unsigned int width,unsigned int height,char* filename)
{
	if (data == NULL) {
		printf("Provide a valid data pointer!\n");
		return;
	}
	if (filename == NULL) {
		printf("Provide a valid filename!\n");		return;
	}
	if ( (width>4096) || (height>4096)) {
		printf("Only pictures upto 4096x4096 are supported!\n");
		return;
	}
	FILE *f=fopen(filename,"wb");
	if (f == NULL) 
	{
		printf("Opening File %s failed!\n",filename);
		return;
	}
	if (fprintf(f,"P6 %i %i 255\n",width,height) <= 0) {
		printf("Writing to file failed!\n");
		return;
	};
	int i;
	for (i=0;i<height;i++) {
		unsigned char buffer[4096*3];
		int j;
		for (j=0;j<width;j++) {
			int v=data[i*width+j];
			int s;
			s= v << 0;
			s=s > 255? 255 : s;
			buffer[j*3+0]=s;
			s= v << 1;
			s=s > 255? 255 : s;
			buffer[j*3+1]=s;
			s= v << 2;
			s=s > 255? 255 : s;
			buffer[j*3+2]=s;
		}
		if (fwrite(buffer,width*3,1,f) != 1) {
			printf("Writing of line %i to file failed!\n",i);
			return;
		}
	}
	fclose(f);
}




int main(int argc, char** args)
{
	unsigned char image [256*256];//???????????????????????
	unsigned char* image_GPU;
	int size_image = sizeof(char) * 256*256;

	int Xnum;
	int Ynum;

	switch (argc){
		case 2:
			Xnum = atoi(args[1]);
			Ynum = 1;
			break;
		case 3:
			Xnum = atoi(args[1]);
			Ynum = atoi(args[2]);
			break;
		default:
			printf("ERROR!!!  Correct usage: ./output <**> (<**>)\n");
	}

	cudaMalloc((void**)&image_GPU, size_image);
	// NOTE: one thread to process one pixel,
	// say we have 16 block, then we need 256*256/16 = 4096  = 16*16*16 threads per block
	// 
	
	dim3 threadsPerBlock(Xnum,Ynum);
	dim3 numBlocks(256/Xnum,256/Ynum);

	//assert(image != NULL);//will terminate the program if image == null
	//calc_fractal(256,256,0.28,0.008,image);//gonna change
	calc_fractal <<<numBlocks, threadsPerBlock>>> (256,256,0.28,0.008,image_GPU);

	cudaMemcpy(image, image_GPU, size_image, cudaMemcpyDeviceToHost);

	write_ppm(image,256,256,"julia.ppm");//not changed
	//free(image);
	return 0;
}
