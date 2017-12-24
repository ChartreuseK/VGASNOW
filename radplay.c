/* RADPLAY - Hayden Kroepfl 2017
 *
 * A simple Reality Adlib tracker file player for DOS.
 *
 * This version modified for use with the SNOW christmas demo
 * 
 * Written for Turbo C 2.01 on a PC/XT Clone.
 */
#include <stdio.h>
#include <conio.h>
#include <stdlib.h>
#include <dos.h>

#define VERSION 			"0.2.1"
#define HEADLEN 			18		/* RAD header length */
#define INSTLEN 			11		/* Length of instrument definition */

#define TIMER50				0x5D38	/* PIT timer for 50Hz */
#define TIMER18				0xFFFF  /* PIT timer for 18.2Hz */

#define CTRL8253			0x43
#define TIMERMODE			0x3C	/* Mode 2, binary count, least/most sig */

#define CNTR0				0x40
#define TIMERVECT			0x1C

/* I/O Ports */
#define AL_ADDR 			0x388
#define AL_DATA 			0x389

/* Adlib delays */
#define DLYR 				6
#define DLYD 				25

#define CHANS				9

/* RAD Commands */
#define CMD_PORTUP		 	1
#define CMD_PORTDN		 	2
#define CMD_TONESLIDE		3
#define CMD_TONEVOLSLIDE 	5
#define CMD_VOLSLIDE		10
#define CMD_SETVOL			12
#define CMD_JMPLINE			13
#define CMD_SETSPEED		15

/* Handy typedefs */
typedef unsigned char uchar;
typedef signed char schar;
typedef unsigned int uint;
typedef unsigned short ushort;


uchar far *screen = (uchar far *)0xB8000000L;


/* Globals */
volatile uchar speed;  /* Current speed */
uchar slow; /* Slow-timer (If set use 18.2Hz interrupt, if not then 50Hz) */

uchar spdcnt; /* Countdown between notes */

volatile uchar running;

/* Instrument table, names are adlib base registers */
struct INST {
	uchar r23;
	uchar r20;
	uchar r43;
	uchar r40;
	uchar r63;
	uchar r60;
	uchar r83;
	uchar r80;
	uchar rC0;
	uchar rE3;
	uchar rE0;
} insts[31];

uchar al_choff[] = {
	0x00, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x10, 0x11, 0x12
};

/* Pattern offset table, pointers to start of pattern in data */
ushort patoff[32];
/* Order list (of patterns to play. Val > 80h = jump */
uchar order[128];
uchar orderlen;
uchar curorder;
uchar curpat;
uchar curline;

/* Pattern/note data */
uchar *data;
uint datalen;
ushort dataoff;

ushort patpos;	/* Offset into data for current pattern */

/* Previous OPL register values for effects, since we can't read back */
uchar prev_vol[CHANS];	   	/* Previous volume values OPL 43h */
uchar prev_freqlow[CHANS];	/* Previous freq values OPL A0h */
uchar prev_freqhigh[CHANS];	/* Previous freq values OPL B0h */

/* Effect/Command parameters */
uchar  toneslide_speed[CHANS];	/* Tone slide speed */
ushort toneslide_freq[CHANS];	/* Tone slide desitination freq */

struct EFFECTS {
	schar portslide;
	uchar toneslide;
	schar volslide;
} effects[CHANS];


char notepr[][3] = {
	"C#","D-","D#","E-",
	"F-","F#","G-","G#",
	"A-","A#","B-","C-",
	"--","--","--","--" };



/* Conversion of note to frequency.
 * C = 0x156 (Low C below octave? We start at C#?)
 *
 * Taken from original Reality Tracker play routine.
 */
ushort notefreq[] = {
	0x16b, 0x181, 0x198, 0x1b0, 0x1ca, 0x1e5,
	0x202, 0x220, 0x241, 0x263, 0x287, 0x2ae
};

/* Range of one octave in frequency */
#define NOTE_C  0x156
#define OCTAVE	(0x2ae - NOTE_C)

/* Convert octave and note to a linearized frequency for slides */
#define linearfreq(oct, note) (((oct)*OCTAVE)+notefreq[(note)]-NOTE_C)
#define linearfreq2(oct, freq) (((oct)*OCTAVE)+(freq)-NOTE_C)


void interrupt (*oldhandler)();


/* Declarations */
void interrupt play();
void doeffects(void);
int do_note(uchar chan, uchar oct, uchar note, uchar cmd, uchar param, uchar inst);
void set_note(uchar chan, uchar oct, uchar note);
void set_linear_freq(uchar chan, short lfreq);
short get_linear_freq(uchar chan);
void set_volume(uchar chan, uchar vol);
uchar get_volume(uchar chan);
void load_inst(uchar i, uchar chan);
int read_data(FILE *fp);
int read_patoff(FILE *fp);
int read_orders(FILE *fp);
int read_insts(FILE *fp);
void print_desc(FILE *fp);
void al_delay(int d);
void al_clr(void);
void al_write(uchar port, uchar val);


extern int snow(void);




int main(int argc, char **argv)
{
	uchar buf[32];
	FILE *rfile;
	int i;

	puts("RADPLAY "VERSION" - Hayden Kroepfl 2017");
	al_clr();

	if (argc < 2) {
		puts("usage: RADPLAY filename.RAD");
		return 1;
	}

	rfile = fopen(argv[1], "rb");
	if (rfile == NULL) {
		puts("Error opening file");
		return 2;
	}

	/* Read in the header */
	if (HEADLEN != fread(buf, 1, HEADLEN, rfile)) {
		puts("Error reading header");
		return 2;
	}
	if (buf[0] != 'R' || buf[1] != 'A' || buf[2] != 'D') {
		puts("Not a RAD file!");
		return 2;
	}
	/* We only support version 1.0 RAD files */
	if (buf[0x10] != 0x10) {
		printf("Invalid RAD version %02x\n", buf[0x10]);
		return 2;
	}

	speed = buf[0x11] & 0x1F;		/* Initial speed */
	slow = (buf[0x11] & 0x40) != 0;	/* Fast(50Hz) or Slow (18.2Hz) */

	if (buf[0x11] & 0x80) {
		/* Read description */
		print_desc(rfile);
	}

	/* Load instruments */
	if (read_insts(rfile) < 0) {
		return -2;
	}

	/* Load orders */
	if (read_orders(rfile) < 0) {
		return -2;
	}

	if (read_patoff(rfile) < 0) {
		return -2;
	}

	if (read_data(rfile) < 0) {
		return -2;
	}

	printf("Data length: %d\n", datalen);

	/* Fixup patoff to be 0 based */
	for (i = 0; i < 32; i++)
		if (patoff[i])
			patoff[i] -= dataoff;

	/* Set PIT Timer 0 to our speed if in fast mode */
	if (!slow) {
		outportb(CTRL8253, TIMERMODE);
		outportb(CNTR0, TIMER50 & 0xFF);
		outportb(CNTR0, TIMER50 >> 8);
		puts("FAST MODE");
	} else {
		puts("SLOW MODE");
	}


	oldhandler = getvect(TIMERVECT);


	/* Begin playback */
	curorder = 0;
	curpat = order[curorder];
	patpos = patoff[curpat];
	running = 1;


	setvect(TIMERVECT, play);

	snow();

	outportb(CTRL8253, TIMERMODE);
	outportb(CNTR0, TIMER18 & 0xFF);
	outportb(CNTR0, TIMER18 >> 8);

	setvect(TIMERVECT, oldhandler);

	al_clr();
	if (data)
		free(data);
	return 0;
}


/*
 * Playback routine
 *
 * Call at 50 or 18.2Hz intervals (depending on fast/slow)
 */
void interrupt play()
{
	uchar line, chan, note[2];
	uchar oct, n, inst, cmd, param;
	int nextline;

	/* Check for done flag */
	if (patpos == 0xFFFF) {
		al_clr();
		running = 0;
		return;
	}

	/*
	 * Read a new line if the count is up
	 */
	if (spdcnt-- == 0) {

		for (chan = 0; chan < CHANS; chan++) {
			effects[chan].portslide = 0;
			effects[chan].toneslide = 0;
			effects[chan].volslide = 0;
		}

		line = data[patpos]; /* Read in line number */
		/*
		 * If the next line read matches the current line number,
		 * ie. We've already handled any blank lines
		 */
		if (curline++ == (line&0x7F)) {
			patpos++;
			do {
				/*
				 * Read all the channel changes in this line
				 */
				chan = data[patpos++];

				note[0] = data[patpos++];
				note[1] = data[patpos++];

				/*
				 * Check for a command, if so read the parameter
				 */
				param = 0;
				if (note[1] & 0xF)
					param = data[patpos++];
				cmd = note[1] & 0xF;

				/*
				 * Extract note data from note packet
				 */
				oct = (note[0] >> 4) & 0x7;
				n = note[0] & 0xF;
				inst = (note[1]>>4) | ((note[0]&0x80)>>3);

				if ((nextline = do_note(chan&0x7F, oct, n, cmd, param, inst)) > 0){
					/*
					 * Jump to line nextline-1 in next pattern, ignore
					 * remaining channels on this line
					 */
					curpat = order[++curorder];
					/*
					 * Check if the next pattern is to be a jump instad
					 */
					while (curpat & 0x80) {
						curorder = curpat - 0x80;
						curpat = order[curorder];
					}

					/* Get the offset for the selected pattern */
					patpos = patoff[curpat];

					/* Go through pattern till we find matching line */
					while ((data[patpos] & 0x7F) < nextline) {
						if (data[patpos] * 0x80) {
							/* End of pattern searching for line
							 * Stop playback
							 */
							patpos = 0xFFFF;
						}
						/* Skip line */
						patpos++;
						while (!(data[patpos++] & 0x80)) {
							patpos++; /* Skip note[0] */
							if (data[patpos++] & 0xF) /* Skip note[1] */
								patpos++; /* Skip parameter */
						}
					}
					curline = nextline;
					goto skip;
				}

			} while (!(chan & 0x80));
		}

		/*
		 * Check if we hit the end of a pattern
		 */
		if ((line & 0x80) || (curline >= 0x80)) {
			curpat = order[++curorder];
			/*
			 * Check if the next pattern is to be a jump instad
			 */
			while (curpat & 0x80) {
				curorder = curpat - 0x80;
				curpat = order[curorder];
			}

			/* Get the offset for the selected pattern */
			patpos = patoff[curpat];
			curline = 0;
		}

		/*
		 * Reset spdcnt to current speed
		 * FOR INITIAL TESTING SET TO 0 SINCE WE HAVE NO EFFECTS
		 */
skip:
		spdcnt = speed-1;
	}

	/* Update effects for the line */
	doeffects();

}

void doeffects(void)
{
	uchar chan;
	short lfreq;
	short vol;

	for (chan = 0; chan < CHANS; chan++) {
		if (effects[chan].portslide) {
			lfreq = get_linear_freq(chan);

			lfreq += (short)(effects[chan].portslide);

			set_linear_freq(chan, lfreq);
		}
		if (effects[chan].toneslide) {
			lfreq = get_linear_freq(chan);
			if (lfreq < toneslide_freq[chan]) {
				lfreq += toneslide_speed[chan];
				if (lfreq >= toneslide_freq[chan]) {
					effects[chan].toneslide = 0;
					lfreq = toneslide_freq[chan];
				}
			} else if (lfreq > toneslide_freq[chan]) {
				lfreq -= toneslide_speed[chan];
				if (lfreq <= toneslide_freq[chan]) {
					effects[chan].toneslide = 0;
					lfreq = toneslide_freq[chan];
				}
			} else {
				effects[chan].toneslide = 0;
			}
			set_linear_freq(chan, lfreq);
		}
		if (effects[chan].volslide) {
			vol = get_volume(chan);
			vol += effects[chan].volslide;
			if (vol < 0)
				vol = 0;
			set_volume(chan, vol);
		}
	}
}

int do_note(uchar chan, uchar oct, uchar note, uchar cmd, uchar param, uchar inst)
{
	/*
	 * If there is a note change
	 */
	if (note) {
		/*
		 * Check if this is a toneslide + note
		 */
		if (cmd == CMD_TONESLIDE) {
			/*
			 * oct+note is the destination frequency
			 */
			toneslide_freq[chan] = linearfreq(oct, note);
			/* If param != 0 then change the speed */
			if (param)
				toneslide_speed[chan] = param;

			effects[chan].toneslide = 1;
			return 0;

		} else {
			/* Set note (or KEY-OFF) */
			set_note(chan, oct, 15); /*KEY-OFF*/
            /*
			 * Change instrument for channel
			 */
			if (inst)
				load_inst(inst-1, chan);
			set_note(chan, oct, note);
		}
	}

	/*
	 * Handle any commands
	 */
	switch(cmd) {
	case CMD_PORTUP:       /* Portamento Up */
		effects[chan].portslide = (schar)param;
		break;

	case CMD_PORTDN:       /* Portamento Down */
		effects[chan].portslide = -(schar)param;
		break;

	case CMD_TONESLIDE:    /* Slide tone (no note specified) */
		effects[chan].toneslide = 1;
		if (param)
			toneslide_speed[chan] = param;
		break;

	case CMD_TONEVOLSLIDE: /* Slide tone and volume */
		effects[chan].toneslide = 1;
		/* Fall through */
	case CMD_VOLSLIDE:     /* Volume slide (Down < 50, Up > 50) */
		effects[chan].volslide = (param < 50) ? -param : param - 50;
		break;

	case CMD_SETVOL:       /* Set volume for channel */
		set_volume(chan, param);
		break;

	case CMD_JMPLINE:      /* Jump to line in next pattern */
		return 1+param;

	case CMD_SETSPEED:	   /* Set playback speed */
		speed = param;
		break;
	}
	return 0;
}

void set_note(uchar chan, uchar oct, uchar note)
{
	ushort freq;

	if (!note)
		return;

	if (note < 13) {
		freq = 0x2000 | (((ushort)oct << 10) + notefreq[note-1]);
		prev_freqlow[chan] = (uchar)freq;
		prev_freqhigh[chan] = (uchar)(freq >> 8);

		al_write(0xA0 + chan, (uchar)freq);
		al_write(0xB0 + chan, (uchar)(freq >> 8));
	}
	else {
		/* KEY-OFF */
		prev_freqhigh[chan] &= ~0x20;
		al_write(0xB0 + chan, prev_freqhigh[chan]);
	}
}

/* Set frequency of channel from a linear freq */
void set_linear_freq(uchar chan, short lfreq)
{
	uchar oct;
	ushort nfreq;
	ushort freq;

	oct = lfreq / OCTAVE;
	nfreq = (lfreq % OCTAVE) + NOTE_C;

	/* Mask out old frequency */
	freq = (prev_freqhigh[chan] & ~0x1F) << 8;
	freq |= nfreq;
	freq |= (ushort)oct << 10;

	prev_freqlow[chan] = (uchar)freq;
	prev_freqhigh[chan] = (uchar)(freq >> 8);

	al_write(0xA0 + chan, (uchar)freq);
	al_write(0xB0 + chan, (uchar)(freq >> 8));

}

/* Get frequency of channel as a linear freq */
short get_linear_freq(uchar chan)
{
	ushort freq, nfreq;
	uchar oct;

	freq = (ushort)prev_freqlow[chan] | ((ushort)prev_freqhigh[chan] << 8);

	oct = (freq >> 10) & 0x7;
	nfreq = freq & 0x3FF;

	return linearfreq2(oct, nfreq);
}

/* Set volume for specified channel */
void set_volume(uchar chan, uchar vol)
{
	uchar new43;
	uchar choff = al_choff[chan];

	if (vol >= 64)
		vol = 63;

	new43 = prev_vol[chan] & ~0x3f;	/* Mask out old volume */
	new43 |= vol ^ 0x3F;			/* Invert volume */

	prev_vol[chan] = new43;
	al_write(0x43+choff, new43);
}

/* Get volume for specified channel */
uchar get_volume(uchar chan)
{
	uchar vol;

	vol = prev_vol[chan] & 0x3F;
	vol ^= 0x3F;

	return vol;
}

/* Load instrument i into OPL channel chan */
void load_inst(uchar i, uchar chan)
{
	uchar choff;
	choff = al_choff[chan];

	al_write(0x23+choff, insts[i].r23);
	al_write(0x20+choff, insts[i].r20);
	al_write(0x43+choff, insts[i].r43);
	prev_vol[chan] = insts[i].r43;
	al_write(0x40+choff, insts[i].r40);
	al_write(0x63+choff, insts[i].r63);
	al_write(0x60+choff, insts[i].r60);
	al_write(0x83+choff, insts[i].r83);
	al_write(0x80+choff, insts[i].r80);
	al_write(0xE3+choff, insts[i].rE3);
	al_write(0xE0+choff, insts[i].rE0);
	al_write(0xC0+chan, insts[i].rC0);
}

/* Read in data section from file */
int read_data(FILE *fp)
{
	long int len, old;

	/* Get length of data section */
	old = ftell(fp);
	fseek(fp, 0, SEEK_END);
	len = ftell(fp) - old;
	fseek(fp, old, SEEK_SET);

	/* Filesize is max 65535, has to fit in one segment */
	if (len > 65535L) {
		puts("Error file too long");
		return -2;
	}
	dataoff = old;

	data = malloc((int)len);
	if (data == NULL) {
		puts("Failed to alloc space for data");
		return -3;
	}

	datalen = fread(data, 1, (int)len, fp);
	if (ferror(fp)) {
		puts("Error reading pattern data");
		return -1;
	}
	return 0;
}

/* Load in pattern offset table */
int read_patoff(FILE *fp)
{
	if (32 != fread(patoff, 2, 32, fp)) {
		puts("Failed to read in pattern offset table.");
		return -1;
	}
	return 0;
}

/* Load in order list */
int read_orders(FILE *fp)
{
	int ch = fgetc(fp);
	orderlen = ch;
	if (ch == EOF || orderlen != fread(order, 1, orderlen, fp)) {
		puts("Error reading orders list");
		return -1;
	}
	return 0;
}


/* Load in instrument table */
int read_insts(FILE *fp)
{
	int ch = fgetc(fp);
	while (ch && ch != EOF) {
		if (INSTLEN != fread(&insts[ch-1], 1, INSTLEN, fp)) {
			printf("Error reading instrument %d\n", ch-1);
			return -1;
		}
		ch = fgetc(fp);
	}
	return 0;
}


/* Print a RAD file description */
void print_desc(FILE *fp)
{
	int ch = fgetc(fp);

	puts("Description:");
	while (ch && ch != EOF) {
		if (ch >= 0x20) {
			putchar(ch);
		} else if (ch == 0x01) {
			puts("");
		} else {
			for (; ch > 0; ch--)
				putchar(' ');
		}

		ch = fgetc(fp);
	}
	puts("");
}

/* Delay function between adlib accesses */
void al_delay(int d)
{
	for (; d > 0; d--)
		inportb(AL_ADDR);
}

/* Write value to OPL2 register */
void al_write(uchar port, uchar val)
{
	outportb(AL_ADDR, port);
	al_delay(DLYR);
	outportb(AL_DATA, val);
	al_delay(DLYD);
}

/* Reset adlib registers */
void al_clr(void)
{
	int i;
	for (i = 0; i < 256; i++)
		al_write(i, 0);
}
