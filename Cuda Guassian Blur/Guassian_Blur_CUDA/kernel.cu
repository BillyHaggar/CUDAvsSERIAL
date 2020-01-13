#include "cuda_runtime.h"
#include "device_launch_parameters.h"
#include "SDL.h"
#include "SDL_image.h"
#undef main

#include <stdio.h>
#include <memory.h>
#include <math.h>
#include <time.h>

/// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> Defines  <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
//Change the width of the tile here (ALSO BLOCK WIDTH)
#define TILE_WIDTH 32

/// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>  Global Variables <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
//change the gaussinan blur effect here
const int maskSize = 3; // width of the blur in pixels, must be an odd value (3, 5, 7, 9, 11, 13)
const float stdv = 20.0; // strength of the blur (1.0, 3.0, 5.0, 10.0, 20.0)?
// Change this to change the image file to be loaded. Note: needs to be a JPEG.
// file sizes are relative to their name, in order from smallest to largest the name's are
// "240p", "480p", "720p", "1080p", "1440p", "4k", "8k", "16k". 
const char* IMAGE_PATH = "4k.jpg";
// Change these to set the size of the window. The aspect ratio should match that of the image
// to be loaded otherwise it'll be distorted.
const int WINDOW_WIDTH = 1280;
const int WINDOW_HEIGHT = 720;
// Host version of the convolution mask 2D array.
float h_convMask[maskSize][maskSize];
// 2D statically allocated array in device memory containing the convolution kernel.
__constant__ float d_convMask[maskSize][maskSize];
// used in the apllying of the convolution kernel, put here in order to set it once
const int offset = (maskSize - 1) / 2; // how many x or y coordinates the convolution kernel will take you away from the central origin

/// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> Functions <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<


/// Generate the guassian convolution kernel
// CPU runnable
// based on: https://www.codewithc.com/gaussian-filter-generation-in-c/
// Modified to be adaptable, the code is messy but the timing of CUDA vs SERIAL will not be affected by this;
// Parameters:
// Width: Dimensional width of the kernel
// Height: Dimensional height of the kernel
////
void generateGuassianKernel(int width, int height) {
	double r, s = 2.0 * stdv * stdv; 
	double sum = 0.0;   // Initialization of sun for normalization

	// Loop to generate kSize x kSize kernel
	for (int x = ((width - 1) / 2) * -1; x <= ((width - 1) / 2); x++) {
		for (int y = ((width - 1) / 2) * -1; y <= ((width - 1) / 2); y++) {
			r = sqrt(x * x + y * y);
			h_convMask[x + ((width - 1) / 2)][y + ((width - 1) / 2)] = (exp(-(r * r) / s)) / (M_PI * s); // generate using the guassian function
			sum += h_convMask[x + ((width - 1) / 2)][y + ((width - 1) / 2)];// used gor normalizing the kernel (See below...)
		}
	}
	for (int i = 0; i < maskSize; ++i) // Loop to normalize the kernel so the image doesnt get dimmer
		for (int j = 0; j < maskSize; ++j)
			h_convMask[i][j] /= sum;
}


////
// Calculate the index of an element index by x,y in a one dimensional
// flattened array (row major), where y is the major axis.
// if an index is calculated to be out of range of the array, set the index to 
// minimum or maximum depending on if the index is below or above the range.
// Parameters:
// x: x coordinate of whole pixel (not accounting for RGBA values).
// y: Y coordinate of whole pixel (not accounting for RGBA values).
// imageW, imageH: width & height of the image.
////
__host__ __device__ int get1dIndex(int width, int height, int x, int y)
{
	//check x and y boundaries
	if (x < 0) 
		x = 0;
	if (x >= width) 
		x = width - 1;
	if (y < 0) 
		y = 0;
	if (y >= height) 
		y = height - 1;

	// check index boundaries
	int i = y * width * 4 + x * 4;
	if (i < 0)
		i = 0;
	if (i > width * height * 4)
		i = ((width * height * 4) - 1);
	return i;
}

////
// CUDA kernel to convolve an image using the convolution kernel
// stored in d_convMask.
// Parameters:
// inPixels: device array holding the original image data.
// outPixels: device array where the modified image should be written.
// imageW, imageH: width & height of the image.
////
__global__ void convolveKernel(float* inPixels, float* outPixels, int imageW, int imageH) {
	int globalIdX = blockIdx.x * blockDim.x + threadIdx.x;
	int globalIdY = blockIdx.y * blockDim.y + threadIdx.y;

	if (globalIdX < imageW && globalIdY < imageH) {
		int i = globalIdX;
		int j = globalIdY;

		// Extract the red, green and blue components.
		//give the position of the r,g,b pixel in the array
		int r = get1dIndex(imageW, imageH, i, j) + 0;
		int g = get1dIndex(imageW, imageH, i, j) + 1;
		int b = get1dIndex(imageW, imageH, i, j) + 2;

		//declare the sum of each pixel colour value, the sum of the pixels around multiplied by the convolution kernels related value
		float rsum = 0.0f;
		float gsum = 0.0f;
		float bsum = 0.0f;

		//loop over guassianKernel
		// x = x of convulutionKernel (Matrix Axis)
		// y = y of convolutionKernel (Matrix Axis)
		for (int x = 0; x < maskSize; x++) {
			for (int y = 0; y < maskSize; y++) {

				//dont operate on values outside array size
				if (!(((i + (x - offset)) < 0) || ((j + (y - offset)) < 0))) { // check top and left pixel overflow
					if (!(((i + (x - offset)) > imageW) || ((j + (y - offset)) > imageH))) { // check bottom and right pixel overflow

						// Get the pixel value for the corresponding kernel value and multiply it buy the convulutionKernel value that relates to it
						rsum += (d_convMask[x][y]) * inPixels[get1dIndex(imageW, imageH, x + (i - offset), y + (j - offset)) + 0];
						gsum += (d_convMask[x][y]) * inPixels[get1dIndex(imageW, imageH, x + (i - offset), y + (j - offset)) + 1];
						bsum += (d_convMask[x][y]) * inPixels[get1dIndex(imageW, imageH, x + (i - offset), y + (j - offset)) + 2];
					}
				}
			}
		}
		//pixels that are now newly calculated now guassian smoothing has been applied
		outPixels[r] = (unsigned char)(fmaxf(0, fminf(rsum, 255.0f)));
		outPixels[g] = (unsigned char)(fmaxf(0, fminf(gsum, 255.0f)));
		outPixels[b] = (unsigned char)(fmaxf(0, fminf(bsum, 255.0f)));
	}
}

////
// GPU version of the convolution code.
// Parameters:
// inPixels: array of bytes containing the original image pixels.
// outPixels: array of bytes where the modified image should be written.
// imageW, imageH: width & height of the image.
////
void convolveImageCuda(float* inPixels, float* outPixels, int imageW, int imageH)
{
	float* d_inPixels; //device copy of inPixels.
	float* d_outPixels; //device copy of outPixels.

	//Allocate device arrays.
	cudaMalloc(&d_inPixels, 4 * imageW * imageH * sizeof(float));
	cudaMalloc(&d_outPixels, 4 * imageW * imageH * sizeof(float));

	//Copy input pixels to device.
	cudaMemcpy(d_inPixels, inPixels, 4 * sizeof(float) * imageH * imageW, cudaMemcpyHostToDevice);

	//Copy convolution mask to device.
	cudaMemcpyToSymbol(d_convMask, h_convMask, sizeof(float) * maskSize * maskSize);

	//Setup the size of blocks and grids kernel.
	int x = ceil((double)imageW / TILE_WIDTH); // Work out how many blocks will be needed in order to cover the whole image
	int y = ceil((double)imageH / TILE_WIDTH);// Work out how many blocks will be needed in order to cover the whole image
	dim3 dimBlock(TILE_WIDTH, TILE_WIDTH, 1); // Create a block size based on tile size
	dim3 dimGrid(x, y, 1); // Create a grid of how many blocks are needed in order to cover the whole image

	// Run the kernel
	convolveKernel << < dimGrid, dimBlock >> > (d_inPixels, d_outPixels, imageW, imageH);


	// Copy results back to outPixels.
	cudaMemcpy(outPixels, d_outPixels, 4 * sizeof(float) * imageH * imageW, cudaMemcpyDeviceToHost);

	//free up memory now not needed
	cudaFree(d_inPixels);
	cudaFree(d_outPixels);
}

////
// CPU version of the convolution code.
// Applies the convolution kernel to each pixel (including RGBA values)
// Parameters:
// inPixels: array of bytes containing the original image pixels.
// outPixels: array of bytes where the modified image should be written.
// imageW, imageH: width & height of the image.
////
void convolveImageCPU(float* inPixels, float* outPixels, int imageW, int imageH)
{
	for (int imageX = 0; imageX < imageW; imageX++) {
		for (int imageY = 0; imageY < imageH; imageY++) {
			if (imageX < imageW && imageY < imageH) {
				int i = imageX;
				int j = imageY;

				// Extract the red, green and blue components.
				//give the position of the r,g,b pixel in the array
				int r = get1dIndex(imageW, imageH, i, j) + 0;
				int g = get1dIndex(imageW, imageH, i, j) + 1;
				int b = get1dIndex(imageW, imageH, i, j) + 2;

				//declare the sum of each pixel colour value, the sum of the pixels around multiplied by the convolution kernels related value
				float rsum = 0.0f;
				float gsum = 0.0f;
				float bsum = 0.0f;

				//loop over guassianKernel
				// x = x of convulutionKernel (Matrix Axis)
				// y = y of convolutionKernel (Matrix Axis)
				for (int x = 0; x < maskSize; x++) {
					for (int y = 0; y < maskSize; y++) {

						//dont operate on values outside array size
						if (!(((i + (x - offset)) < 0) || ((j + (y - offset)) < 0))) { // check top and left pixel overflow
							if (!(((i + (x - offset)) > imageW) || ((j + (y - offset)) > imageH))) { // check bottom and right pixel overflow

								// Get the pixel value for the corresponding kernel value and multiply it buy the convulutionKernel value that relates to it
								rsum += (h_convMask[x][y]) * inPixels[get1dIndex(imageW, imageH, x + (i - offset), y + (j - offset)) + 0];
								gsum += (h_convMask[x][y]) * inPixels[get1dIndex(imageW, imageH, x + (i - offset), y + (j - offset)) + 1];
								bsum += (h_convMask[x][y]) * inPixels[get1dIndex(imageW, imageH, x + (i - offset), y + (j - offset)) + 2];
							}
						}
					}
				}
				//pixels that are now newly calculated now guassian smoothing has been applied
				outPixels[r] = (unsigned char)(fmaxf(0, fminf(rsum, 255.0f)));
				outPixels[g] = (unsigned char)(fmaxf(0, fminf(gsum, 255.0f)));
				outPixels[b] = (unsigned char)(fmaxf(0, fminf(bsum, 255.0f)));
			}
		}
	}
}

////
// Program entry point.
////
int main(int argc, int** argv)
{
	generateGuassianKernel(maskSize, maskSize);
	// Initialize SDL and create window.
	SDL_Init(SDL_INIT_VIDEO);
	SDL_Window* window = SDL_CreateWindow(
		"Guassian Blur Applicator, CPU vs GPU",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		WINDOW_WIDTH, WINDOW_HEIGHT, 0);
	SDL_Renderer* renderer = SDL_CreateRenderer(
		window,
		-1,
		SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

	// Load a photo based on image path set at the top.
	SDL_Surface* image = IMG_Load(IMAGE_PATH);
	printf("Loaded %dx%d image.\n", image->w, image->h);
	// Copy to a new surface so that we know the format (32 bit RGBA).
	SDL_Surface* surface = SDL_CreateRGBSurface(0, image->w, image->h, 32, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);
	SDL_BlitSurface(image, NULL, surface, NULL);
	SDL_FreeSurface(image);
	image = NULL;
	//retreve the image size from the surface of the SDL panel
	int imageSize = surface->w * surface->h;
	
	// Allocate a pointer and space in memory for pixel data from the surface,
	// contains the RGBA values for every pixel repeeated over and over.
	// Note: stored in row major order
	float* floatPixels;
	cudaMallocHost(&floatPixels, 4 * imageSize * sizeof(float));
	float* floatPixelsOut;
	cudaMallocHost(&floatPixelsOut, 4 * imageSize * sizeof(float));
	float* floatPixelsStore;
	cudaMallocHost(&floatPixelsStore, 4 * imageSize * sizeof(float));

	// Copy surface data (image)
	unsigned char* surfacePixels = (unsigned char*)surface->pixels;
	for (int i = 0; i < imageSize; i++) {
		floatPixels[i * 4 + 0] = ((float)surfacePixels[i * 4 + 0]);
		floatPixels[i * 4 + 1] = ((float)surfacePixels[i * 4 + 1]);
		floatPixels[i * 4 + 2] = ((float)surfacePixels[i * 4 + 2]);
	}

	//CPU run and time
	clock_t CPUStart = clock();
	convolveImageCPU(floatPixels, floatPixelsOut, surface->w, surface->h);
	clock_t CPUEnd = clock();
	float CPUms = 1000.0f * (CPUEnd - CPUStart) / CLOCKS_PER_SEC;
	printf("CPU Convolution took %fms.\n\n", CPUms);

	//Store a copy of CPU out pixel ready to compare to GPU pixels
	for (int i = 0; i < imageSize; i++) {
		floatPixelsStore[i * 4 + 0] = ((float)floatPixelsOut[i * 4 + 0]);
		floatPixelsStore[i * 4 + 1] = ((float)floatPixelsOut[i * 4 + 1]);
		floatPixelsStore[i * 4 + 2] = ((float)floatPixelsOut[i * 4 + 2]);
	}

	//GPU run and time
	clock_t GPUStart = clock();
	convolveImageCuda(floatPixels, floatPixelsOut, surface->w, surface->h);
	clock_t GPUEnd = clock();
	float GPUms = 1000.0f * (GPUEnd - GPUStart) / CLOCKS_PER_SEC;
	printf("GPU Convolution took %fms.\n\n", GPUms);

	int correctPixels = 0;
	//Compare Results
	for (int i = 0; i < imageSize; i++) {
		if (floatPixelsStore[i * 4 + 0] == ((float)floatPixelsOut[i * 4 + 0])) {
			if (floatPixelsStore[i * 4 + 1] == ((float)floatPixelsOut[i * 4 + 1])) {
				if (floatPixelsStore[i * 4 + 2] == ((float)floatPixelsOut[i * 4 + 2])) {
					correctPixels++;
				}
			}
		}
	}

	float difference = ((correctPixels / imageSize) * 100);
	printf("CPU vs GPU Convolution Simularity %f Percent \n\n", difference);

	// Allocate a texture that will be the actual image drawn to the screen.
	SDL_Texture* texture = SDL_CreateTexture(
		renderer,
		SDL_PIXELFORMAT_ABGR8888,
		SDL_TEXTUREACCESS_STREAMING,
		surface->w, surface->h);

	unsigned char* pixelsTmp;
	int pitch;

	SDL_LockTexture(texture, NULL, (void**)(&pixelsTmp), &pitch);

	// put the pixels from the calulations of the convolve kernel in pixelstmp ready
	// to render the image
	for (int i = 0; i < imageSize; i++) {
		pixelsTmp[i * 4 + 0] = (unsigned char)(floatPixelsOut[i * 4]);
		pixelsTmp[i * 4 + 1] = (unsigned char)(floatPixelsOut[i * 4 + 1]);
		pixelsTmp[i * 4 + 2] = (unsigned char)(floatPixelsOut[i * 4 + 2]);
	}

	SDL_UnlockTexture(texture);

	// Draw the image.
	SDL_RenderCopy(renderer, texture, NULL, NULL);
	SDL_RenderPresent(renderer);


	// Main loop - runs continually until quit.
	bool running = true;
	while (running) {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_QUIT) {
				// User pressed the "X", Alt-4F, etc...
				running = false;
			}
		}
	}

	// Main loop finished - quit.
	cudaDeviceSynchronize();
	SDL_DestroyTexture(texture);
	SDL_FreeSurface(surface);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();

	// Free Up Memory (D_pixels deallocated in convolveImageCuda)
	cudaFree(floatPixels);
	cudaFree(floatPixelsOut);
	cudaFree(floatPixelsStore);
	cudaFree(d_convMask);


	return 0;
}
