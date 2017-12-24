/* SNOW - Hayden Kroepfl 2017
 * 
 * A simple snowfall particle simulation for Mode 13h VGA
 * This demo is CPU SPEED DEPENDENT! On a 10MHz XT, 200 particles
 * with the provided RADPLAY ISR running runs at a nice speed.
 * 
 * You may want to add delays or some form of vsync to the loop if you want
 * to run this on more than an XT class system, alternatively you can adjust
 * MAX_PARTICLES to slow the simulation down somewhat, though there'll be more
 * snow on the screen at any given time.
 * 
 * As the snow reaches the top of the screen the density will increase as there
 * are always 200 particles falling at once. Once the screen is full the program
 * will hang and not respond to input, don't let this happen. This is due to the
 * infinite loop tying to randomly find a space to spawn the particle. Either
 * fix this loop to have an exit, or make it such that the snow doesn't reach 
 * the top of the screen.
 * 
 * This file was written for Turbo C 2.0 on a Turbo PC/XT clone.
 * 
 * RADPLAY.C contains the main routine which will call this demo, to run this
 * without the RAD player, simply rename int snow(void) to int main() and 
 * compile just this file.
 * 
 * The files vgatree and vgamerry.h contain the image data displayed for the 
 * snow to fall on top.
 */
#include <dos.h>
#include <conio.h>

#include "vgatree.h"
#include "vgamerry.h"

char far *scr = (char far *)0xA0000000;

typedef unsigned int uint;
typedef unsigned char uchar;
typedef signed char schar;
typedef unsigned short ushort;

/* scr[y * 320 + x] */
#define px(x,y) *(scr + (((y)<<8) + ((y)<<6) + (x)))

#define MAX_PARTICLES	200


struct PARTICLE {
	int x, y;
} particles[MAX_PARTICLES];


int snow(void)
{
	union REGS regs;
	uint i, j;
	int cx, cy;


	/* Initialize Mode 13h VGA */
	regs.h.ah = 0x00;
	regs.h.al = 0x13;
	int86(0x10, &regs, &regs);

	/* Clear screen */
	for (i = 0; i < 32000; i++)
		*(((ushort far *)scr)+i) = 0x0000;

	/* Draw initial drawings for snow to fall on */
#define TREEX 40
#define TREEY (199-TREEHGT)
	for (i = 0; i < TREEHGT; i++)
		for (j = 0; j < TREEWID; j++)
			px(TREEX+j, TREEY+i) = tree[i*TREEWID + j];
#define MERRYX 170
#define MERRYY 120
	for (i = 0; i < MERRYHGT; i++)
		for (j = 0; j < MERRYWID; j++)
			px(MERRYX+j, MERRYY+i) = merry[i*MERRYWID + j];


	for (i = 0; i < MAX_PARTICLES; i++) {
		do {
			cx = particles[i].x = rand() % 320;
			cy = particles[i].y = i / (MAX_PARTICLES/200);
		} while (px(cx,cy) != 0);

		px(particles[i].x, particles[i].y) = 0xF;
	}


	/* Update particles */
	while (!kbhit()) {
		for (i = 0; i < MAX_PARTICLES; i++) {
			cx = particles[i].x;
			cy = particles[i].y;

			if (cy == 199 || px(cx,cy+1) != 0) {
				/* Try and spread out first */
				if (cx != 0 && cy != 199 && px(cx-1,cy+1) == 0) {
					/* Move down and left */
					px(cx, cy) = 0;
					cx--; cy++;
					px(cx, cy) = 0xF;
					particles[i].x = cx;
					particles[i].y = cy;
				} else if (cx != 319 && cy != 199 && px(cx+1,cy+1) == 0) {
					/* Move down and right */
					px(cx, cy) = 0;
					cx++; cy++;
					px(cx, cy) = 0xF;
					particles[i].x = cx;
					particles[i].y = cy;
				} else {
					/* Halt particle by removing from list */
					for (j = i; j < MAX_PARTICLES-1; j++) {
						particles[j] = particles[j+1];
					}
					/* Replace with new particle */
					do {
						cx = particles[MAX_PARTICLES-1].x = rand() % 320;
					} while (px(cx, 0) != 0);
					cy = particles[MAX_PARTICLES-1].y = 0;
					px(cx, cy) = 0xF;
				}
			} else {
				/* Move particle down */
				px(cx, cy) = 0;
				cy++;
				px(cx, cy) = 0xF;
				particles[i].y = cy;
			}
		}
	}





	while (!kbhit())
		;

	/* Return to 80x25 colour text mode */
	regs.h.ah = 0x00;
	regs.h.al = 0x03;
	int86(0x10, &regs, &regs);

	return 0;
}
