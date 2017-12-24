/* RCGL C Graphics Library
 *******************************************************************************
BSD 3-Clause License

Copyright (c) 2017, Hayden Kroepfl
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the copyright holder nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ******************************************************************************/

#include "rcgl.h"
#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdlib.h>


/* LIBRARY STATE */
static SDL_Window *wind;
static SDL_Renderer *rend;
static SDL_Texture *tx;
static SDL_Thread *thread;

static SDL_cond *initcond;
static SDL_cond *waitdrawcond;
static SDL_mutex *mutex;
static int initstatus;
static int drawstatus;

static SDL_atomic_t status;

static uint32_t EVENT_TERM;
static uint32_t EVENT_REDRAW;

uint32_t rcgl_palette[256];
static int bw;                  // Buffer width
static int bh;                  // Buffer height
static uint8_t *buf;                   // Pointer to user buffer
static uint8_t *ibuf;                  // Internal/Default user buffer
static int running;				// Is the video thread still alive

static struct CARGS {
	int w, h, ww, wh;
	const char *title;
	int wflags;
} cargs;


/* Internal prototypes */
static void blit(uint8_t *src, uint32_t *dst);
static int videothread(void *data);



/* EXPORTED LIBRARY ROUTINES */


/*
 * rcgl_init - Initialize library and create window
 * Buffer will be scaled to fill window size
 * 
 * w - buffer width
 * h - buffer height
 * ww - window width
 * wh - window height
 * title - title
 * sc - integer pixel scale (window size is w*sc by h*sc)
 * wflags:  1 = RESIZABLE, 2 = FULLSCREEN, 4 = MAXIMIZED,
 *          8 = FULLSCREEN_NATIVE, 16 = INTEGER SCALING
 */
int rcgl_init(int w, int h, int ww, int wh, const char *title, int wflags)
{
	int rval = 0;
	int istat;

	bw = w;
	bh = h;

	cargs.w = w;
	cargs.h = h;
	cargs.ww = ww;
	cargs.wh = wh;
	cargs.title = title;
	cargs.wflags = wflags;

	// Create internal framebuffer
	if ((ibuf = calloc(w*h, sizeof(uint8_t))) == NULL) {
		fprintf(stderr, "RCGL: Failed to allocate internal framebuffer\n");
		rval = -1;
		goto failalloc;
	}
	buf = ibuf;

	// Set default palette
	rcgl_setpalette(RCGL_PALETTE_VGA);

	mutex = SDL_CreateMutex();
	if (mutex == NULL) {
		fprintf(stderr, "RCGL: Failed to create mutex\n");
		rval = -2;
		goto failmutex;
	}
	initcond = SDL_CreateCond();
	if (initcond == NULL) {
		fprintf(stderr, "RCGL: Failed to create init condition variable\n");
		rval = -2;
		goto failcond;
	}
	waitdrawcond = SDL_CreateCond();
	if (waitdrawcond == NULL) {
		fprintf(stderr, "RCGL: Failed to create wdraw condition variable\n");
		rval = -2;
		goto failcond2;
	}

	// Create user defined events
	EVENT_TERM = SDL_RegisterEvents(2);
	if (EVENT_TERM == (uint32_t)-1) {
		fprintf(stderr, "RCGL: Failed to create user events\n");
		rval = -2;
		goto failevent;
	}
	EVENT_REDRAW = EVENT_TERM+1;

	
	
	// Start-up video thread
	thread = SDL_CreateThread(videothread, "RCGLWindowThread", NULL);
	if (thread == NULL) {
		fprintf(stderr, "RCGL: Failed to create RCGLWindowThread: %s\n",
		        SDL_GetError());
		rval = -3;
		goto failthread;
	}

	// Block till video thread has been initialized, or till an error occurs
	SDL_LockMutex(mutex);
	while (!initstatus) {
		SDL_CondWait(initcond, mutex);
	}
	istat = initstatus;
	SDL_UnlockMutex(mutex);
	if (istat < 0) { // Failure to init
		fprintf(stderr, "RCGL: Error intializing in video thread\n");
		SDL_WaitThread(thread, &rval);
		goto failthread;
	}
	// Otherwise video thread has been launched successfully
	// Clear the screen
	rcgl_update();
	
	return rval;
	// Failure path
failthread:
failevent:
	SDL_DestroyCond(waitdrawcond);
failcond2:
	SDL_DestroyCond(initcond);
failcond:
	SDL_DestroyMutex(mutex);
failmutex:
	free(ibuf);
	ibuf = NULL;
	buf = NULL;
failalloc:
	return rval;
}

/*
 * rcgl_quit - Terminate program manually
 */
void rcgl_quit(void)
{
	int rval = 0;
	// Signal to video thread to close down shop
	SDL_Event event;
	SDL_zero(event);
	event.type = EVENT_TERM;
	SDL_PushEvent(&event);

	// Wait for video thread to quit
	SDL_WaitThread(thread, &rval);
	
	// Finally destroy our buffer
	if (ibuf)
		free(ibuf);
	ibuf = NULL;
}

/*
 * rcgl_update - Render buffer to screen
 */
int rcgl_update(void)
{
	void *rbuf;
	int rval = 0;
	

	SDL_Event event;
	SDL_zero(event);
	event.type = EVENT_REDRAW;
	SDL_PushEvent(&event);

	// Wait for thread to draw changes before returning
	SDL_LockMutex(mutex);
	SDL_CondWait(waitdrawcond, mutex);

	rval = drawstatus;
	SDL_UnlockMutex(mutex);

	return rval;
}

/*
 * rcgl_setbuf - Change buffer to b
 * If b is NULL, set buffer to internal buffer
 */
void rcgl_setbuf(uint8_t *b)
{
	if (b)
		buf = b;
	else
		buf = ibuf;
}

/*
 * rcgl_getbuf - Get pointer to the current buffer
 */
uint8_t *rcgl_getbuf(void)
{
	return buf;
}

/*
 * rcgl_hasquit - Test if program has quit
 */
int rcgl_hasquit(void)
{
	return SDL_AtomicGet(&status) == 0;
}

/*
 * rcgl_delay - Delay for ms milliseconds
 */
void rcgl_delay(uint32_t ms)
{
	SDL_Delay(ms);
}

/*
 * rcgl_ticks - Return number of milliseconds since start
 */
uint32_t rcgl_ticks(void)
{
	return SDL_GetTicks();
}

/*
 * rcgl_plot - Plot a pixel at x,y with colour c
 */
void rcgl_plot(int x, int y, uint8_t c)
{
	buf[y * bw + x] = c;
}

/*
 * rcgl_setpalette - Copy an entire palette definition into the current palette
 */
void rcgl_setpalette(const uint32_t palette[256])
{
	for (int i = 0; i < 256; i++)
		rcgl_palette[i] = palette[i];
}

/*
 * rcgl_line - Draw a line between two points
 */
void rcgl_line(int x1, int y1, int x2, int y2, uint8_t c)
{
	// Bresenham's line drawing algorithm
	int dx, dy;
	int adx, ady;
	int x, y;
	int sdx, sdy;
	int ex, ey;

	dx = x2 - x1;
	dy = y2 - y1;

	// With the abs we can pretend to only be in octant 1 or 0
	adx = abs(dx);
	ady = abs(dy);

	// Figure out the actual octant for the line
	sdx = (dx > 0) ? 1 : (dx < 0) ? -1 : 0;
	sdy = (dy > 0) ? 1 : (dy < 0) ? -1 : 0;

	x = x1;
	y = y1;

	ex = 0;
	ey = 0;

	if (adx >= ady) { // Octant 0 (y rises slower than x)
		for (int i = 0; i <= adx; i++) {
			rcgl_plot(x, y, c);

			ey += ady;
			if (ey >= adx) { // If we're past the increment point of y
				ey -= adx;   // Reset, but propogate error
				y += sdy;
			}
			x += sdx;
		}
	}
	else { // Octant 1 (x rises slower than y)
		for (int i = 0; i <= ady; i++) {
			rcgl_plot(x, y, c);

			ex += adx;
			if (ex >= ady) { // If we're past the increment point of x
				ex -= ady;   // Reset, but propogate error
				x += sdx;
			}
			y += sdy;
		}
	}
}

/*
 * rcgl_blit - Blit a bitmap somewhere onto the framebuffer
 */
void rcgl_blit(uint8_t *b, int x, int y, int w, int h, int trans, uint8_t *plt)
{
	uint8_t *fb = buf + (y * bw) + x;

	if (plt != NULL) {
		for (int r = 0; r < h; r++) {
			for (int c = 0; c < w; c++) {
				if (trans < 0 || plt[*b] != trans)
					*fb = plt[*b];
				b++;
				fb++;
			}
			fb += bw-w;
		}
	}
	else {
		for (int r = 0; r < h; r++) {
			for (int c = 0; c < w; c++) {
				if (trans < 0 || *b != trans)
					*fb = *b;
				b++;
				fb++;
			}
			fb += bw-w;
		}
	}
}


/* INTERNAL LIBRARY HELPER ROUTINES */

/*
 * blit - Render 8-bit bitmap to 32-bit bitmap using palette
 */
static void blit(uint8_t *src, uint32_t *dst)
{
	for (int y = 0; y < bh; y++)
		for (int x = 0; x < bw; x++)
			*(dst++) = rcgl_palette[*(src++)] | 0xFF000000;
}

/*
 * Background and screen update handler
 *
 * NOTE: This is the only thread allowed to call WaitEvent/PollEvent/PumpEvents
 * Though to do so we need to move the SDL init code here :/
 */
static int videothread(void *data)
{
	int rval;
	SDL_Event event;
	int pitch;
	void *rbuf;
	int dstatus;

	/* Video initialization */
	SDL_Init(SDL_INIT_VIDEO);
	wind = SDL_CreateWindow(cargs.title,
	           SDL_WINDOWPOS_UNDEFINED,
	           SDL_WINDOWPOS_UNDEFINED,
	           cargs.ww,
	           cargs.wh,
	           ((cargs.wflags&RCGL_RESIZE)?SDL_WINDOW_RESIZABLE:0)
	           | ((cargs.wflags&RCGL_FULLSCREEN)?SDL_WINDOW_FULLSCREEN:0)
	           | ((cargs.wflags&RCGL_MAXIMIZED)?SDL_WINDOW_MAXIMIZED:0)
	           | ((cargs.wflags&RCGL_FULLSCREEN_NATIVE)?SDL_WINDOW_FULLSCREEN_DESKTOP:0)
	           | SDL_WINDOW_ALLOW_HIGHDPI);
	if (wind == NULL) {
		fprintf(stderr, "RCGL: Failed to create Window: %s\n",
		        SDL_GetError());
		rval = -4;
		goto failwind;
	}
	
	rend = SDL_CreateRenderer(wind, -1, 0);
	if (rend == NULL) {
		fprintf(stderr, "RCGL: Failed to create Renderer: %s\n",
		        SDL_GetError());
		rval = -4;
		goto failrend;
	}
	SDL_RenderSetLogicalSize(rend, cargs.w, cargs.h);
	SDL_RenderSetIntegerScale(rend, cargs.wflags & RCGL_INTSCALE);
	
	tx = SDL_CreateTexture(rend,
	                       SDL_PIXELFORMAT_ARGB8888,
	                       SDL_TEXTUREACCESS_STREAMING,
	                       cargs.w,
	                       cargs.h);
	if (tx == NULL) {
		fprintf(stderr, "RCGL: Failed to create Texture: %s\n",
		        SDL_GetError());
		rval = -4;
		goto failtx;
	}

	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, 0);
	SDL_SetRenderDrawColor(rend, 0, 0, 0, 0);
	SDL_RenderClear(rend);
	SDL_GL_SetSwapInterval(1);

	// Clear initial window
	SDL_SetRenderDrawColor(rend, 0, 0, 0, 255);
	SDL_RenderClear(rend);
	SDL_RenderPresent(rend);              // Do update

	SDL_AtomicSet(&status, 1);

	// Signal to parent thread that initialization has been successful
	SDL_LockMutex(mutex);
	initstatus = 1;
	SDL_CondBroadcast(initcond);
	SDL_UnlockMutex(mutex);

	
	
	running = 1;
	while (running) {
		if (SDL_WaitEvent(&event)) {
			// Handle events
			do {
				if (event.type == EVENT_REDRAW) {
					dstatus = 1;
					if (0 == SDL_LockTexture(tx, NULL, &rbuf, &pitch)) 
						blit(buf, (uint32_t *)rbuf);	  // Palettize and copy to texture
					else // Otherwise Failed to open texture, couldn't render.
						dstatus = 0;
						
					SDL_UnlockTexture(tx);
					SDL_SetRenderDrawColor(rend, 0, 0, 0, 0);
					SDL_RenderClear(rend);
					SDL_RenderCopy(rend, tx, NULL, NULL); // Render texture to entire window
					SDL_RenderPresent(rend);              // Do update

					// Let update method return now that we're done
					SDL_LockMutex(mutex);
					SDL_CondBroadcast(waitdrawcond);
					drawstatus = dstatus;
					SDL_UnlockMutex(mutex);
				}
				else if (event.type == EVENT_TERM) {
					SDL_AtomicSet(&status, 0);
					running = 0;
				}
				else switch (event.type) {
				case SDL_QUIT:
					SDL_AtomicSet(&status, 0);
					running = 0;
					break;
				case SDL_WINDOWEVENT:
					// Assume something happened to the window, so just redraw
					SDL_SetRenderDrawColor(rend, 0, 0, 0, 0);
					SDL_RenderClear(rend);
					SDL_RenderCopy(rend, tx, NULL, NULL); // Render texture to entire window
					SDL_RenderPresent(rend);              // Do update
					break;
				
				}
			} while (SDL_PollEvent(&event));
		}
	}
	
failalloc:
	SDL_DestroyTexture(tx);
failtx:
	SDL_DestroyRenderer(rend);
failrend:
	SDL_DestroyWindow(wind);
failwind:
	SDL_Quit();

	// Signal to parent thread that we failed
	if (rval < 0) {
		SDL_LockMutex(mutex);
		initstatus = rval;
		SDL_CondBroadcast(initcond);
		SDL_UnlockMutex(mutex);
	}
	
	return rval;
}


/* Palettes */

const uint32_t RCGL_PALETTE_VGA[256] = {
0x00000000, 0x000000aa, 0x0000aa00, 0x0000aaaa, 0x00aa0000, 0x00aa00aa, 
0x00aa5500, 0x00aaaaaa, 0x00555555, 0x005555ff, 0x0055ff55, 0x0055ffff, 
0x00ff5555, 0x00ff55ff, 0x00ffff55, 0x00ffffff, 0x00000000, 0x00141414, 
0x00202020, 0x002c2c2c, 0x00383838, 0x00454545, 0x00515151, 0x00616161, 
0x00717171, 0x00828282, 0x00929292, 0x00a2a2a2, 0x00b6b6b6, 0x00cbcbcb, 
0x00e3e3e3, 0x00ffffff, 0x000000ff, 0x004100ff, 0x007d00ff, 0x00be00ff, 
0x00ff00ff, 0x00ff00be, 0x00ff007d, 0x00ff0041, 0x00ff0000, 0x00ff4100, 
0x00ff7d00, 0x00ffbe00, 0x00ffff00, 0x00beff00, 0x007dff00, 0x0041ff00, 
0x0000ff00, 0x0000ff41, 0x0000ff7d, 0x0000ffbe, 0x0000ffff, 0x0000beff, 
0x00007dff, 0x000041ff, 0x007d7dff, 0x009e7dff, 0x00be7dff, 0x00df7dff, 
0x00ff7dff, 0x00ff7ddf, 0x00ff7dbe, 0x00ff7d9e, 0x00ff7d7d, 0x00ff9e7d, 
0x00ffbe7d, 0x00ffdf7d, 0x00ffff7d, 0x00dfff7d, 0x00beff7d, 0x009eff7d, 
0x007dff7d, 0x007dff9e, 0x007dffbe, 0x007dffdf, 0x007dffff, 0x007ddfff, 
0x007dbeff, 0x007d9eff, 0x00b6b6ff, 0x00c7b6ff, 0x00dbb6ff, 0x00ebb6ff, 
0x00ffb6ff, 0x00ffb6eb, 0x00ffb6db, 0x00ffb6c7, 0x00ffb6b6, 0x00ffc7b6, 
0x00ffdbb6, 0x00ffebb6, 0x00ffffb6, 0x00ebffb6, 0x00dbffb6, 0x00c7ffb6, 
0x00b6ffb6, 0x00b6ffc7, 0x00b6ffdb, 0x00b6ffeb, 0x00b6ffff, 0x00b6ebff, 
0x00b6dbff, 0x00b6c7ff, 0x00000071, 0x001c0071, 0x00380071, 0x00550071, 
0x00710071, 0x00710055, 0x00710038, 0x0071001c, 0x00710000, 0x00711c00, 
0x00713800, 0x00715500, 0x00717100, 0x00557100, 0x00387100, 0x001c7100, 
0x00007100, 0x0000711c, 0x00007138, 0x00007155, 0x00007171, 0x00005571, 
0x00003871, 0x00001c71, 0x00383871, 0x00453871, 0x00553871, 0x00613871, 
0x00713871, 0x00713861, 0x00713855, 0x00713845, 0x00713838, 0x00714538, 
0x00715538, 0x00716138, 0x00717138, 0x00617138, 0x00557138, 0x00457138, 
0x00387138, 0x00387145, 0x00387155, 0x00387161, 0x00387171, 0x00386171, 
0x00385571, 0x00384571, 0x00515171, 0x00595171, 0x00615171, 0x00695171, 
0x00715171, 0x00715169, 0x00715161, 0x00715159, 0x00715151, 0x00715951, 
0x00716151, 0x00716951, 0x00717151, 0x00697151, 0x00617151, 0x00597151, 
0x00517151, 0x00517159, 0x00517161, 0x00517169, 0x00517171, 0x00516971, 
0x00516171, 0x00515971, 0x00000041, 0x00100041, 0x00200041, 0x00300041, 
0x00410041, 0x00410030, 0x00410020, 0x00410010, 0x00410000, 0x00411000, 
0x00412000, 0x00413000, 0x00414100, 0x00304100, 0x00204100, 0x00104100, 
0x00004100, 0x00004110, 0x00004120, 0x00004130, 0x00004141, 0x00003041, 
0x00002041, 0x00001041, 0x00202041, 0x00282041, 0x00302041, 0x00382041, 
0x00412041, 0x00412038, 0x00412030, 0x00412028, 0x00412020, 0x00412820, 
0x00413020, 0x00413820, 0x00414120, 0x00384120, 0x00304120, 0x00284120, 
0x00204120, 0x00204128, 0x00204130, 0x00204138, 0x00204141, 0x00203841, 
0x00203041, 0x00202841, 0x002c2c41, 0x00302c41, 0x00342c41, 0x003c2c41, 
0x00412c41, 0x00412c3c, 0x00412c34, 0x00412c30, 0x00412c2c, 0x0041302c, 
0x0041342c, 0x00413c2c, 0x0041412c, 0x003c412c, 0x0034412c, 0x0030412c, 
0x002c412c, 0x002c4130, 0x002c4134, 0x002c413c, 0x002c4141, 0x002c3c41, 
0x002c3441, 0x002c3041, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 
0x00000000, 0x00000000, 0x00000000, 0x00000000 };

const uint32_t RCGL_PALETTE_GREY[256] = {
0x00000000, 0x00010101, 0x00020202, 0x00030303, 0x00040404, 0x00050505, 
0x00060606, 0x00070707, 0x00080808, 0x00090909, 0x000a0a0a, 0x000b0b0b, 
0x000c0c0c, 0x000d0d0d, 0x000e0e0e, 0x000f0f0f, 0x00101010, 0x00111111, 
0x00121212, 0x00131313, 0x00141414, 0x00151515, 0x00161616, 0x00171717, 
0x00181818, 0x00191919, 0x001a1a1a, 0x001b1b1b, 0x001c1c1c, 0x001d1d1d, 
0x001e1e1e, 0x001f1f1f, 0x00202020, 0x00212121, 0x00222222, 0x00232323, 
0x00242424, 0x00252525, 0x00262626, 0x00272727, 0x00282828, 0x00292929, 
0x002a2a2a, 0x002b2b2b, 0x002c2c2c, 0x002d2d2d, 0x002e2e2e, 0x002f2f2f, 
0x00303030, 0x00313131, 0x00323232, 0x00333333, 0x00343434, 0x00353535, 
0x00363636, 0x00373737, 0x00383838, 0x00393939, 0x003a3a3a, 0x003b3b3b, 
0x003c3c3c, 0x003d3d3d, 0x003e3e3e, 0x003f3f3f, 0x00404040, 0x00414141, 
0x00424242, 0x00434343, 0x00444444, 0x00454545, 0x00464646, 0x00474747, 
0x00484848, 0x00494949, 0x004a4a4a, 0x004b4b4b, 0x004c4c4c, 0x004d4d4d, 
0x004e4e4e, 0x004f4f4f, 0x00505050, 0x00515151, 0x00525252, 0x00535353, 
0x00545454, 0x00555555, 0x00565656, 0x00575757, 0x00585858, 0x00595959, 
0x005a5a5a, 0x005b5b5b, 0x005c5c5c, 0x005d5d5d, 0x005e5e5e, 0x005f5f5f, 
0x00606060, 0x00616161, 0x00626262, 0x00636363, 0x00646464, 0x00656565, 
0x00666666, 0x00676767, 0x00686868, 0x00696969, 0x006a6a6a, 0x006b6b6b, 
0x006c6c6c, 0x006d6d6d, 0x006e6e6e, 0x006f6f6f, 0x00707070, 0x00717171, 
0x00727272, 0x00737373, 0x00747474, 0x00757575, 0x00767676, 0x00777777, 
0x00787878, 0x00797979, 0x007a7a7a, 0x007b7b7b, 0x007c7c7c, 0x007d7d7d, 
0x007e7e7e, 0x007f7f7f, 0x00808080, 0x00818181, 0x00828282, 0x00838383, 
0x00848484, 0x00858585, 0x00868686, 0x00878787, 0x00888888, 0x00898989, 
0x008a8a8a, 0x008b8b8b, 0x008c8c8c, 0x008d8d8d, 0x008e8e8e, 0x008f8f8f, 
0x00909090, 0x00919191, 0x00929292, 0x00939393, 0x00949494, 0x00959595, 
0x00969696, 0x00979797, 0x00989898, 0x00999999, 0x009a9a9a, 0x009b9b9b, 
0x009c9c9c, 0x009d9d9d, 0x009e9e9e, 0x009f9f9f, 0x00a0a0a0, 0x00a1a1a1, 
0x00a2a2a2, 0x00a3a3a3, 0x00a4a4a4, 0x00a5a5a5, 0x00a6a6a6, 0x00a7a7a7, 
0x00a8a8a8, 0x00a9a9a9, 0x00aaaaaa, 0x00ababab, 0x00acacac, 0x00adadad, 
0x00aeaeae, 0x00afafaf, 0x00b0b0b0, 0x00b1b1b1, 0x00b2b2b2, 0x00b3b3b3, 
0x00b4b4b4, 0x00b5b5b5, 0x00b6b6b6, 0x00b7b7b7, 0x00b8b8b8, 0x00b9b9b9, 
0x00bababa, 0x00bbbbbb, 0x00bcbcbc, 0x00bdbdbd, 0x00bebebe, 0x00bfbfbf, 
0x00c0c0c0, 0x00c1c1c1, 0x00c2c2c2, 0x00c3c3c3, 0x00c4c4c4, 0x00c5c5c5, 
0x00c6c6c6, 0x00c7c7c7, 0x00c8c8c8, 0x00c9c9c9, 0x00cacaca, 0x00cbcbcb, 
0x00cccccc, 0x00cdcdcd, 0x00cecece, 0x00cfcfcf, 0x00d0d0d0, 0x00d1d1d1, 
0x00d2d2d2, 0x00d3d3d3, 0x00d4d4d4, 0x00d5d5d5, 0x00d6d6d6, 0x00d7d7d7, 
0x00d8d8d8, 0x00d9d9d9, 0x00dadada, 0x00dbdbdb, 0x00dcdcdc, 0x00dddddd, 
0x00dedede, 0x00dfdfdf, 0x00e0e0e0, 0x00e1e1e1, 0x00e2e2e2, 0x00e3e3e3, 
0x00e4e4e4, 0x00e5e5e5, 0x00e6e6e6, 0x00e7e7e7, 0x00e8e8e8, 0x00e9e9e9, 
0x00eaeaea, 0x00ebebeb, 0x00ececec, 0x00ededed, 0x00eeeeee, 0x00efefef, 
0x00f0f0f0, 0x00f1f1f1, 0x00f2f2f2, 0x00f3f3f3, 0x00f4f4f4, 0x00f5f5f5, 
0x00f6f6f6, 0x00f7f7f7, 0x00f8f8f8, 0x00f9f9f9, 0x00fafafa, 0x00fbfbfb, 
0x00fcfcfc, 0x00fdfdfd, 0x00fefefe, 0x00ffffff };


