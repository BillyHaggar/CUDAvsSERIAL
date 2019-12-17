#include "SDL.h"
#include "SDL_image.h"
#undef main

#include <stdio.h>
#include <memory.h>
#include <math.h>
#include <time.h>
// Enter value here to create the size of the kernel (width and height) MUST BE AN ODD VALUE
#define kSize 11 // Basically the width of the blur in pixels
#define stdv 10.0 // strength of the blur

///Set the image and window dimensions
// File path of the image to load;
// Choose "plymouth2.jpg" for lower resolution or otherwise use "plymouth.jpg"
const char* IMAGE_PATH = "plymouth2.jpg";
// Set the width and height of the window here, must match the aspect ratio of the image to avoid distortion;
const int WINDOW_WIDTH = 1024;
const int WINDOW_HEIGHT = 591;

bool guassianBlur = false; // global so multilple functions can access it


float guassianKernel[kSize][kSize]; //create a empty Kernel

/// Generate the guassian convolution kernel
// CPU runnable
// based on: https://www.codewithc.com/gaussian-filter-generation-in-c/
// Modified to be adaptable, the code is messy but the timing of CUDA vs SERIAL will not be affected by this;
// Parameters:
// Width: Dimensional width of the kernel
// Height: Dimensional height of the kernel
////
void generateGuassianKernel(int width, int height) {
	double r, s = 2.0 * stdv * stdv;  // Assigning standard deviation to 1.0
	double sum = 0.0;   // Initialization of sun for normalization

	// Loop to generate kSize x kSize kernel
	for (int x = ((width - 1) / 2) * -1; x <= ((width - 1) / 2); x++) {
		for (int y = ((width - 1) / 2) * -1; y <= ((width - 1) / 2); y++){
			r = sqrt(x * x + y * y);
			guassianKernel[x + ((width - 1) / 2)][y + ((width - 1) / 2)] = (exp(-(r * r) / s)) / (M_PI * s); // generate using the guassian function
			sum += guassianKernel[x + ((width - 1) / 2)][y + ((width - 1) / 2)];// used gor normalizing the kernel (See below...)
		}
	}
	for (int i = 0; i < kSize; ++i) // Loop to normalize the kernel so the image doesnt get dimmer
		for (int j = 0; j < kSize; ++j)
			guassianKernel[i][j] /= sum;
}

/// function to change the image pixels
// CPU version of the image modification code.
// Parameters:
// inPixels: array of bytes containing the original image pixels.
// outPixels: array of bytes where the modified image should be written.
// imageW, imageH: width & height of the image.
// brightness, contrast: new image parameters to use.
////
void changeImageCPU(unsigned char* inPixels, unsigned char* outPixels, int imageW, int imageH, float brightness, float contrast)
{
	// Iterate through every pixel. Note that each pixel is four bytes (red, green, blue, alpha)
	// so we add 4 to i each time.
	for (int i = 0; i < imageW * imageH * 4; i += 4) {
		
		// Extract the red, green and blue components.
		float r = (float)inPixels[i + 0];
		float g = (float)inPixels[i + 1];
		float b = (float)inPixels[i + 2];

		// Simple formula for adjusting brightness and contrast.
		r = contrast * (r - 128.0f) + 128.0f + brightness;
		g = contrast * (g - 128.0f) + 128.0f + brightness;
		b = contrast * (b - 128.0f) + 128.0f + brightness;

		// Write the new pixel values back out (constrained to the range 0-255).
		outPixels[i + 0] = (unsigned char)(fmax(0, fmin(r, 255.0f)));
		outPixels[i + 1] = (unsigned char)(fmax(0, fmin(g, 255.0f)));
		outPixels[i + 2] = (unsigned char)(fmax(0, fmin(b, 255.0f)));

	}
		
	printf("Brightness Or Contrast CPU Adjustment Applied, ");
}

/// find the 1d index of pixels based upon its x and y pixel location
// Runnable on CPU
// Parameters:
// width: width of the image
// x: x location of pixel
// y: y location of pixel
////
int get1dIndex(int width, int x, int y) {
	return y * width * 4 + x * 4;
}


/// function to change the image pixels with a guassian blur based on the 
/// guassian blur convultion Kernel
// CPU version of the image modification code.
// Parameters:
// inPixels: array of bytes containing the original image pixels.
// outPixels: array of bytes where the modified image should be written.
// imageW, imageH: width & height of the image.
////
void guassianBlurCPU(unsigned char* inPixels, unsigned char* outPixels, int imageW, int imageH) {
	
	int offset = (kSize - 1) / 2; // how many x or y coordinates the convolution kernel will take you away from the central origin

	// Iterate through every pixel location.
	// i = pixelx (Matrix Axis)
	// j = pixely (Matrix Axis)
	for (int i = 0; i < imageW; i++) {
		for (int j = 0; j < imageH; j++) {
			// Extract the red, green and blue components.
			//give the position of the r,g,b pixel in the array
			int r = get1dIndex(imageW, i, j) + 0;
			int g = get1dIndex(imageW, i, j) + 1;
			int b = get1dIndex(imageW, i, j) + 2;

			//declare the sum of each pixel colour value, the sum of the pixels around multiplied by the convolution kernels related value
			float rsum = 0.0f;
			float gsum = 0.0f;
			float bsum = 0.0f;

			//loop over guassianKernel
			// x = x of convulutionKernel (Matrix Axis)
			// y = y of convolutionKernel (Matrix Axis)
			for (int x = 0; x < kSize; x++) {
				for (int y = 0; y < kSize; y++) {

					//dont operate on values outside array size
					if (! (((i + (x - offset)) < 0) || ((j + (y - offset)) < 0)) ) { // check top and left pixel overflow
						if (!(((i + (x - offset)) > imageW) || ((j + (y - offset)) > imageH))) { // check bottom and right pixel overflow

							// Get the pixel value for the corresponding kernel value and multiply it buy the convulutionKernel value that relates to it
							rsum += (guassianKernel[x][y]) * inPixels[get1dIndex(imageW, x + (i - offset), y + (j - offset)) + 0];
							gsum += (guassianKernel[x][y]) * inPixels[get1dIndex(imageW, x + (i - offset), y + (j - offset)) + 1];
							bsum += (guassianKernel[x][y]) * inPixels[get1dIndex(imageW, x + (i - offset), y + (j - offset)) + 2];
						}
					}
				}
			}

			//pixels that are now newly calculated now guassian smoothing has been applied
			outPixels[r] = (unsigned char)(fmax(0, fmin(rsum, 255.0f)));
			outPixels[g] = (unsigned char)(fmax(0, fmin(gsum, 255.0f)));
			outPixels[b] = (unsigned char)(fmax(0, fmin(bsum, 255.0f)));
		}
	}
	printf("Guassian Blur CPU Adjustment Applied, ");
}


///initialise everyting ready to change the image, call the change of image and then 
///calculate how long to change, as well as unlock the texture.
// Parameters:
// surface: An SDL_Surface containing the original image to be modified.
// texture: An SDL_Texture where the modified image should be written.
// brightness, contrast: Values indicating the new image parameters.
////
void changeImage(SDL_Surface* surface, SDL_Texture* texture, float brightness, float contrast)
{
	unsigned char* pixelsTmp;
	int pitch;

	SDL_LockTexture(texture, NULL, (void**)(&pixelsTmp), &pitch);
	clock_t tStart; // declared here but used just before and after functions for accurate timing.
	clock_t tEnd;
	
	if (!guassianBlur) {
		tStart = clock();
		changeImageCPU((unsigned char*)(surface->pixels), pixelsTmp, surface->w, surface->h, brightness, contrast);
		tEnd = clock();
	}
	else {
		tStart = clock();
		guassianBlurCPU((unsigned char*)(surface->pixels), pixelsTmp, surface->w, surface->h);
		tEnd = clock();
	}

	float ms = 1000.0f * (tEnd - tStart) / CLOCKS_PER_SEC;
	printf("Adjustment took %fms.\n\n", ms);
	SDL_UnlockTexture(texture);
}


////
// Program entry point.
////
int main(int argc, int** argv)
{
	generateGuassianKernel(kSize, kSize);

	for (int x = 0; x < kSize; x++) {
		for (int y = 0; y < kSize; y++) {
			printf("%0.8f, ", guassianKernel[x][y]);
		}
		printf("\n");
	}


	// Initialize SDL and create window.
	SDL_Init(SDL_INIT_VIDEO);
	SDL_Window* window = SDL_CreateWindow("Image Brightness, Contrast and Guassian Smoothing Adjuster", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT, 0);
	SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

	// Load a photo.
	SDL_Surface* image = IMG_Load(IMAGE_PATH);
	printf("Loaded %dx%d image.\n\n", image->w, image->h);
	// Copy to a new surface so that we know the format (32 bit RGBA).
	SDL_Surface* surface = SDL_CreateRGBSurface(0, image->w, image->h, 32, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);
	SDL_BlitSurface(image, NULL, surface, NULL);
	SDL_FreeSurface(image);
	image = NULL;

	// Allocate a texture that will be the actual image drawn to the screen.
	SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, surface->w, surface->h);

	// Set initial parameters.
	float brightness = 0.0f;
	float contrast = 1.0f;

	// Call the change of Image
	changeImage(surface, texture, brightness, contrast);

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
			else if (event.type == SDL_KEYDOWN) {
				// A key was pressed, maybe adjust parameters.
				bool parametersChanged = true;
				
				if (event.key.keysym.sym == SDLK_LEFT) {
					contrast -= 0.1f;
				}
				else if (event.key.keysym.sym == SDLK_RIGHT) {
					contrast += 0.1f;
				}
				else if (event.key.keysym.sym == SDLK_DOWN) {
					brightness -= 10.0f;
				}
				else if (event.key.keysym.sym == SDLK_UP) {
					brightness += 10.0f;
				}
				else if (event.key.keysym.sym == SDLK_1) {
					guassianBlur = !guassianBlur; // toggle whether the guassian blur is applied or not
				}
				else {
					parametersChanged = false;
				}

				// only change the image when the user has requested so
				if (parametersChanged) {
					// Update the image using new brightness/contrast, either on the CPU or GPU.
					changeImage(surface, texture, brightness, contrast);
					// Draw texture to the screen.
					SDL_RenderCopy(renderer, texture, NULL, NULL);
					SDL_RenderPresent(renderer);
				}
			}
		}
	}

	// Main loop finished - quit.
	SDL_DestroyTexture(texture);
	SDL_FreeSurface(surface);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();

	return 0; // exit code for exiting with everything ok
}
