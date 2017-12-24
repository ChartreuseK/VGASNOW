/* RCGL C Graphics Library
 *
 * A simple graphics library wrapper for SDL2 providing a simple linear
 * frame-buffer interface with palettized 256-color.  Designed to allow quick
 * prototyping with simple pixel graphics routines much like back in the DOS
 * VGA days.
 *
 * To link with your program (with gcc), include rcgl.h and compile
 *   gcc -o <prog> <source files> rcgl.c -lSDL2
 *
 * Why a wrapper on top of SDL2? Why not just use it directly?
 *
 * Mainly because getting started with SDL2 is quite slow! Especially if you
 * want to just plot pixels straight to a framebuffer and have them appear on
 * screen. It took jumping between many forums, blog posts, and the SDL2
 * documentation to create this library. I don't want to have to do this every
 * time I want to mess around with some simple pixel graphics. I'm sure others
 * just want a simple quick way to toss 8-bit pixel data to the screen.
 * 
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
#ifndef RCGL_H
#define RCGL_H

#include <stdint.h>

#define RCGL_RESIZE	    1
#define RCGL_FULLSCREEN 2
#define RCGL_MAXIMIZED  4
#define RCGL_FULLSCREEN_NATIVE 8
#define RCGL_INTSCALE	16

extern uint32_t rcgl_palette[256];

extern const uint32_t RCGL_PALETTE_VGA[256];
extern const uint32_t RCGL_PALETTE_GREY[256];

int rcgl_init(int w, int h, int ww, int wh, const char *title, int wflags);
void rcgl_quit(void);
int rcgl_update(void);
void rcgl_setbuf(uint8_t *b);
uint8_t *rcgl_getbuf(void);
int rcgl_hasquit(void);
void rcgl_delay(uint32_t ms);
uint32_t rcgl_ticks(void);
void rcgl_plot(int x, int y, uint8_t c);
void rcgl_setpalette(const uint32_t palette[256]);
void rcgl_line(int x1, int y1, int x2, int y2, uint8_t c);
void rcgl_blit(uint8_t *b, int x, int y, int w, int h, int trans, uint8_t *plt);

#endif
