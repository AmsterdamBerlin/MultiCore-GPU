#include <stdio.h>
#include <stdlib.h>
#include <string.h>

__constant__ const unsigned int s_table[] = {
7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22 ,
5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20 ,
4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23 ,
6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21 };

__constant__ const unsigned int k_table[] = {
0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee ,
0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501 ,
0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be ,
0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821 ,
0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa ,
0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8 ,
0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed ,
0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a ,
0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c ,
0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70 ,
0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05 ,
0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665 ,
0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039 ,
0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1 ,
0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1 ,
0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391 };



const int digests_3letters[] = {
0xbc519d9f, 0xca21ef70, 0x07f3145c, 0xd8290a98, 
0xe9475db1, 0x63ee3198, 0xf37cf4e3, 0x9a8e47d4, 
0x76dab734, 0x98d2214b, 0x047d30ef, 0xc52d15d8, 
0x1c77e47f, 0xeb228a00, 0x7df43d76, 0xaac6e219, 
0x06a3c1b8, 0x7e246791, 0xdaf00335, 0x23576cba, 
0x2285c2d4, 0x1d539374, 0x0a147705, 0x6439d01e, 
0x4b90f674, 0x5fdedeb8, 0xc2dcad8e, 0xce312a0a, 
0xb2211727, 0x6962b154, 0xf5c3146e, 0xe3f86d5a, 
0xb860bfb3, 0xb2aeeb51, 0xa3018b76, 0x2ff32e2e, 
0x747d75a4, 0x483bff19, 0x59902ee9, 0x48750e6f 
};


const int digests_4letters[] = {
0x03c82e91, 0xe449ceb2, 0x8d0641a5, 0x70b55a49, 
0xe26f4639, 0x342a062b, 0x3c9fe0cf, 0x6848c2c8, 
0xd8235545, 0xb71a8a6a, 0x0832d3c7, 0xe71902fe, 
0x8fec9be7, 0xa1f82f9e, 0xf7382938, 0xcc585413, 
0x283b084b, 0xa1858b5a, 0x455602e6, 0xe39cd3da, 
0xe8ffa0f2, 0x4fd4c83e, 0x24b6e42b, 0xde7df4b0, 
0xc9418d85, 0xfab897e3, 0x6d04bb34, 0x76f25580, 
0x4a6bcc95, 0xaddeabc5, 0xa8734bc7, 0xd90072ba, 
0xa4ae137c, 0xde6d6e7d, 0xc0d262fd, 0xa4b25306, 
0xf567183e, 0x4530e8ae, 0x35be5f77, 0xe13c6a5e 
};

//opencl, vulkan, mcaaca, bigbox, 
const int digests_6letters[] = {
0xb15d1993, 0x8f7873ad, 0x509361e9, 0x87887d3a, 
0x9ef17384, 0x21e22965, 0x759e4557, 0x85f4cbb4, 
0x8770cff1, 0x1ebae88f, 0x0e30356d, 0x5bc404db, 
0xe6679a67, 0x69bea3a1, 0x7c534292, 0x1dec5ee1 
};
	
#define MAX_DG (10)
#define COMB 26*26*26*26
#define Xnum 26
#define Ynum 26
#define Lnum 3  // number of letters

__device__ void md5(char* message,int length, int* digest) 		// Simplified for max. 8 letters
{
	 unsigned int a0 = 0x67452301;
	unsigned int b0 = 0xefcdab89; 
   unsigned int c0 = 0x98badcfe; 
   unsigned int d0 = 0x10325476; 
	unsigned int A=a0;
	unsigned int B=b0;
	unsigned int C=c0;
	unsigned int D=d0;
	unsigned int M[16]  = {0,0,0,0, 0,0,0,0, 0,0,0,0 , 0,0,0,0};
	memcpy(M,message,length);
	((char*)M)[length]=0x80;
	M[14]=length*8;
	for (int i=0;i<64;i++) 
	{
		unsigned int F = (B & C) | ((~B) & D);
		unsigned int G = (D & B) | ((~D) & C);
		unsigned int H = B ^ C ^ D;
		unsigned int I = C ^ (B | (~D));
		unsigned int tempD = D;
		D = C;
		C = B;
		unsigned int X=I;
		unsigned int g=(7*i) & 15;
		if (i < 48) { X = H; g=(3*i+5) & 15; }
		if (i < 32) { X = G; g=(5*i+1) & 15; }
		if (i < 16) { X = F; g=i; }

		unsigned int tmp = A+X+k_table[i]+M[g];
		B = B + ((tmp << s_table[i]) | ((tmp & 0xffffffff) >> (32-s_table[i])));
		A = tempD;
	}
   digest[0] = a0 + A;
   digest[1] = b0 + B;
   digest[2] = c0 + C;
   digest[3] = d0 + D;
   /* unsigned int a0 = 0x67452301;
	unsigned int b0 = 0xefcdab89; 
    unsigned int c0 = 0x98badcfe; 
    unsigned int d0 = 0x10325476; 
	unsigned int A=a0;
	unsigned int B=b0;
	unsigned int C=c0;
	unsigned int D=d0;
	unsigned int M[16]  = {0,0,0,0, 0,0,0,0, 0,0,0,0 , 0,0,0,0};
	memcpy(M,message,length);
	((char*)M)[length]=0x80;
	M[14]=length*8;
	
	#pragma unroll
	for(int i = 0; i < 16; i++){
		unsigned int X = (B & C) | ((~B) & D);
		unsigned int g = i;
		unsigned int tmp = A+X+k_table[i]+M[g];
		A = D;
		D = C;
		C = B;	
		B = B + ((tmp << s_table[i]) | ((tmp & 0xffffffff) >> (32-s_table[i])));
	}

	#pragma unroll
	for(int i = 16; i < 32; i++){
		unsigned int X = (D & B) | ((~D) & C);
		unsigned int g = (5*i+1) & 15;;
		unsigned int tmp = A+X+k_table[i]+M[g];
		A = D;
		D = C;
		C = B;
		B = B + ((tmp << s_table[i]) | ((tmp & 0xffffffff) >> (32-s_table[i])));
	}


	#pragma unroll
	for(int i = 32; i < 48; i++){
		unsigned int X = B ^ C ^ D;
		unsigned int g = (3*i+5) & 15;
		unsigned int tmp = A+X+k_table[i]+M[g];
		A = D;
		D = C;
		C = B;
		B = B + ((tmp << s_table[i]) | ((tmp & 0xffffffff) >> (32-s_table[i])));
	}

	#pragma unroll
	for(int i = 48; i < 64; i++){
		unsigned int X = C ^ (B | (~D));
		unsigned int g = (7*i) & 15;
		unsigned int tmp = A+X+k_table[i]+M[g];		
		A = D;
		D = C;
		C = B;
		B = B + ((tmp << s_table[i]) | ((tmp & 0xffffffff) >> (32-s_table[i])));
	}

   digest[0] = a0 + A;
   digest[1] = b0 + B;
   digest[2] = c0 + C;
   digest[3] = d0 + D;*/
}


__global__ void check_password(char *digests_GPU, const int *digests, int num_digests)
{
	int dg[4];
	char passwd_temp[Lnum+1];
 	passwd_temp[0] = 'a' + blockIdx.x;
 	//passwd_temp[1] = 'a' + blockIdx.y;
 	passwd_temp[1] = 'a' + threadIdx.x;
 	passwd_temp[2] = 'a' + threadIdx.y;

 	passwd_temp[Lnum] = 0;
	// char to int
	md5(passwd_temp,Lnum,dg);
	for (int i=0;i< num_digests; i++)
	{	
		//&& ( dg[3] == digests[i*4+3] )
		if (( dg[0] == digests[i*4] ) && ( dg[1] == digests[i*4+1] ) && ( dg[2] == digests[i*4+2] ) ) {
			memcpy(&digests_GPU[i * (Lnum+1)], passwd_temp, Lnum+1);
		}
	}
}

// totally 26*26*26*26 = 456976 combinations 
// for 26*26*26 : we have 26 * 26 * 26
int main(int argc, char** args) 
{
	char passwd[MAX_DG][Lnum+1]; 
	char* digests_GPU;

	// allocate memory space in GPU for found passwords
	cudaMalloc((char**)&digests_GPU, MAX_DG*(Lnum+1));

	cudaMemset(digests_GPU, 'a', MAX_DG*(Lnum+1)*sizeof(char));

	dim3 threadsPerBlock(Xnum,Ynum);
	dim3 numBlocks(Xnum,1);

	check_password <<<numBlocks, threadsPerBlock>>> (digests_GPU, digests_3letters, MAX_DG);
		
	cudaMemcpy(passwd, digests_GPU, MAX_DG*(Lnum+1), cudaMemcpyDeviceToHost);

	for (int i = 0; i < MAX_DG; i++)
	{
		printf("%i: %s\n", i, passwd[i]);
		
	}	
}

