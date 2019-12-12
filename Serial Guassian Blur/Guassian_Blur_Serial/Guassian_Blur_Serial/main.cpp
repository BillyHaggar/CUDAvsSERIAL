#include "SDL.h"
#include "SDL_image.h"
#undef main

#include <stdio.h>
#include <memory.h>
#include <math.h>
#include <time.h>

///Set the image and window dimensions
// File path of the image to load;
const char* IMAGE_PATH = "plymouth.jpg";
// Set the width and height of the window here, must match the aspect ratio of the image to avoid distortion;
const int WINDOW_WIDTH = 1024;
const int WINDOW_HEIGHT = 591;

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

		//TODO add guassian blur here!;
		//------------------------------

		// Write the new pixel values back out (constrained to the range 0-255).
		outPixels[i + 0] = (unsigned char)(fmax(0, fmin(r, 255.0f)));
		outPixels[i + 1] = (unsigned char)(fmax(0, fmin(g, 255.0f)));
		outPixels[i + 2] = (unsigned char)(fmax(0, fmin(b, 255.0f)));
	}
}

///initialise everyting ready to change the image, call the change of image and then 
///calculate how long to change, as well as unlock the texture.
// Parameters:
// surface: An SDL_Surface containing the original image to be modified.
// texture: An SDL_Texture where the modified image should be written.
// brightness, contrast: Values indicating the new image parameters.
///
void changeImage(SDL_Surface* surface, SDL_Texture* texture, float brightness, float contrast)
{
	unsigned char* pixelsTmp;
	int pitch;

	SDL_LockTexture(texture, NULL, (void**)(&pixelsTmp), &pitch);
	clock_t tStart = clock();

	changeImageCPU((unsigned char*)(surface->pixels), pixelsTmp, surface->w, surface->h, brightness, contrast);

	clock_t tEnd = clock();
	float ms = 1000.0f * (tEnd - tStart) / CLOCKS_PER_SEC;
	printf("Adjustment took %fms.\n", ms);

	SDL_UnlockTexture(texture);
}

////
// Program entry point.
////
int main(int argc, int** argv)
{
	// Initialize SDL and create window.
	SDL_Init(SDL_INIT_VIDEO);
	SDL_Window* window = SDL_CreateWindow("Image Brightness, Contrast and Guassian smoothing Adjuster", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT, 0);
	SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

	// Load a photo.
	SDL_Surface* image = IMG_Load(IMAGE_PATH);
	printf("Loaded %dx%d image.\n", image->w, image->h);
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
