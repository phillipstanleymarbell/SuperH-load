/*								     	*/
/*  eEK/eOS loader for Hitachi SuperH boards with Advanced Monitor 1.0 	*/
/*   Copyright (C) 1999, Phillip Stanley-Marbell. All rights reserved. 	*/
/*      This software is provided with ABSOLUTELY NO WARRANTY. 	     	*/
/*								     	*/

/*									*/
/*			           NOTES				*/
/*	1. Whenever the board sends us a message that has a checksum, 	*/
/*	we must respond with a + or -, otherwise, everything gets 	*/
/*	screwed up							*/
/*									*/

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/uio.h>

struct termios	newsetting;

enum
{
	ROMSTRLEN = 64,
	PORTNAMELEN = 16,
	MAXRESPONSELEN = 1024,
	MAXLINELEN = 1024,
	MAXPKTERROR = 65535,
	MAXFILENAMELEN = 64,
};

typedef struct
{
	char romversion[ROMSTRLEN];
	long baudrate;
	char port[PORTNAMELEN];
	char status[MAXRESPONSELEN];
	unsigned com;
} Board;

enum
{
	ERROR = -1,
	OK = 1,

	ENQ = 5,	/* For CMON 'MAGIC' PROTOCOL */
	ACK = 6,	/* For CMON 'MAGIC' PROTOCOL */
};

static char	*cksum(char *, char*);
static int	init(Board *);
static int	send(char *, Board *);
static int	receive(Board *);
static void	load(Board *);
static void	run(Board *);
static int	hextoint(char);
static int	asciihextobyte(char *);
static void	regdump(Board *);
static void	memdump(Board *);
static void	term(Board *);
static void	hint(Board *);
static void	reset(Board *);
static void	query_offsets(Board *);


int
hextoint(char hex)
{
	if (hex >= '0' && hex <= '9')
		return (hex - '0');
	else	
	if (hex >= 'a' && hex <= 'f')
		return (hex - 'a' + 10);
	else
	if (hex >= 'A' && hex <= 'F')
		return (hex - 'A' + 10);
	else
		return (0);
}

int
asciihextobyte(char *c)
{
	return ((hextoint(*c)<<4)|hextoint(*(++c)));
}

int
init(Board *board)
{
	strncpy(board->romversion, "", ROMSTRLEN);
	strncpy(board->port, "/dev/cua00", PORTNAMELEN);
	strncpy(board->status, "", MAXRESPONSELEN);

	board->baudrate = B9600 | CS8 | CREAD;
	board->com = 0;

	if ((board->com = open(board->port, O_RDWR|O_NDELAY)) == ERROR)
	{
		fprintf(stderr, "shload : ERROR - Could not open %s\n", board->port);
		exit(-1);
	}

	if (ioctl(board->com, TIOCGETA, &newsetting) < 0)
	{
		perror(0);
		fprintf(stderr, "shload : ERROR - ioctl GET failed on %s\n", board->port);
		exit(-1);
	}

	newsetting.c_iflag = 0L;
	newsetting.c_oflag = 0L;
	newsetting.c_cflag = board->baudrate;
	newsetting.c_lflag = 0L;
	newsetting.c_cc[VTIME] = 0;
	newsetting.c_cc[VMIN] = 0;

	if (ioctl(board->com, TIOCSETA, &newsetting) < 0)
	{
		perror(0);
		fprintf(stderr, "shload : ERROR - ioctl SET failed on %s\n", board->port);
		exit(-1);
	}

	return OK;
}

char *
cksum (char *pkt, char *checkstr)
{
	int checkval = 0;
 	char *tptr;

	tptr = pkt;
	while (*tptr != '\0')
	{
		checkval += *tptr++;
	}

	tptr = checkstr;
	/* anding with 0xf masks off the upper bits past the 4th bit */
	*tptr++ = "0123456789abcdef"[(checkval >> 4) & 0xf];
	*tptr++ = "0123456789abcdef"[checkval & 0xf];

	return checkstr;
}

int
send (char *command, Board *board)
{
	int timeout = 0, n = 0;
 	char *tptr, *buf;


	buf = (char *) malloc(sizeof(char)*3);

	/* Send the leading '$' : */
	tptr = buf;
	strncpy(tptr, "$", 1);
	while (*tptr != '\0' && timeout < MAXPKTERROR)
	{
		n = 0;
		do
		{
			n = write(board->com, tptr, 1);
			timeout++;
		}
		while (n != 1);
		*tptr++;
	}

	/* Send the command : */
	tptr = command;
	while (*tptr != '\0' && timeout < MAXPKTERROR)
	{
		n = 0;
		do
		{
			n = write(board->com, tptr, 1);
			timeout++;
		}
		while (n != 1);
		*tptr++;
	}

	/* Send the leading '#' : */
	tptr = buf;
	strncpy(tptr, "#", 1);
	while (*tptr != '\0' && timeout < MAXPKTERROR)
	{
		n = 0;
		do
		{
			n = write(board->com, tptr, 1);
			timeout++;
		}
		while (n != 1);
		*tptr++;
	}

	/* Send the checksum : */
	tptr = buf;
	cksum(command,tptr);
	while (*tptr != '\0' && timeout < MAXPKTERROR)
	{
		n = 0;
		do
		{
			n = write(board->com, tptr, 1);
			timeout++;
		}
		while (n != 1);
		*tptr++;
	}

	return ((timeout >= MAXPKTERROR) ? ERROR : OK); 
}


int
receive(Board *board)
{
	int n = 0, timeout = 0, responselen = 0;
	char readch, *tptr, cksum[2];

	tptr = board->status;
	while ((readch != '#') && (timeout < MAXPKTERROR) && (responselen < MAXRESPONSELEN))
	{
		n = read(board->com, &readch, 1);
		if (n == 0)
		{
			timeout++;
		}
		else
		{
			*tptr++ = readch;
			responselen++;
			n = 0;
		}
	}
	*tptr = '\0';

	n = 0;
	while (n < 2)
	{
		if (read(board->com, &readch, 1))
		{
			cksum[n++] = readch;
		}
	}

	if (strstr(board->status, "#") != NULL)
	{
		/* for now, we just assume packet is sane */
		send("+", board);
	}

	return ((timeout >= MAXPKTERROR) ? ERROR : OK); 
}

int
main(int argc, char *argv[])
{
	Board *board;
	char cmd=0, buf[16], *tptr;

	system("clear");
  	printf("\n\neEK/eOS loader for Hitachi superH series boards with Advanced Monitor v1.0. and CMON v3.4\n");
  	printf("Copyright (C) 1999, Phillip Stanley-Marbell. All Rights Reserved.\n");
  	printf("This software is provided with ABSOLUTELY NO WARRANTY.\n\n");

	if ((board = (Board *) malloc(sizeof(Board))) == NULL)
	{
		fprintf(stderr, "shload : ERROR - no memory\n");
		exit(-1);
	}

	init(board);

	if (argc > 1) 
	{
             if (strncmp(argv[1], "9600", 4) == 0) 
	     {
	     }
             else 
	     {
	        printf("Usage: shload [9600 | 19200 | 38400] \n");
		exit(1);
	     }
	}
        else 
	{
	     printf("shload : Using %s at 9600 baud\n", board->port);
	}

	tptr = board->romversion;
	if (send("qID", board) == OK)
	{
		if (receive(board) == OK)
		{	
			strncpy(board->romversion, board->status, MAXRESPONSELEN); 
			printf("ROM version : %s\n", board->romversion);
		}
		else
		{
			fprintf(stderr, "shload : ERROR - Could not get ROM version");
		}
	}
	else
	{
		fprintf(stderr, "shload : ERROR - Could not Initialize Board.");
	}

  	while (cmd != 'q') 
	{
	  	printf("\nCommands :\n");
  		printf("v - Print Rom Version\n");
  		printf("q - Quit\n");
  		printf("l - Load S-Record File\n");
  		printf("g - Go !\n");
		printf("r - Read Registers\n");
		printf("o - Query Offsets\n");
		printf(". - Reset\n");
		printf("m - Dump Memory\n");
		printf("t - Start terminal on serial port\n");
		printf("h - Download code to board w/ CMON monitor\n");
		printf("d - Run and start terminal on serial port\n");
  		printf("shload >> ");

		rewind(stdin);
  		fgets(buf, 2, stdin);
  		cmd = buf[0];

		switch(cmd) 
		{
			case 'q':
				break;

			case 'l':
				load(board);
				break;

			case 'g': 
				run(board);
				break;

			case 'r': 
				regdump(board);
				break;

			case 'v': 
				fprintf(stdout, "\nBoard ROM Version : %s\n", board->romversion);
				break;

			case '.':
				reset(board);
				break;

			case 'o':
				query_offsets(board);
				break;

			case 'm':
				memdump(board);
				break;

			case 't':
				term(board);
				break;

			case 'h':
				hint(board);
				break;

			case 'd':
				run(board);
				term(board);
				break;

			default :
			   	break;
		}
  	}

	return 0;
}

void
load(Board *board)
{
	FILE *fd;
	long loadaddr = 0;
	char filename[MAXFILENAMELEN], temp_line [MAXLINELEN], buf[MAXLINELEN+16];
	char *pkt, *stat, *tptr, *response;
	int srecordtype = 0, pktlen = 0, npackets = 0;


	pkt = (char *) malloc(sizeof(char)*MAXLINELEN);
	response = (char *) malloc(sizeof(char)*MAXLINELEN);

	printf("S-Record File: ");
	scanf("%64s", filename);
	
	fd = fopen(filename, "r");
	if (fd == NULL)
	{
		fprintf(stderr, "shload : ERROR - Could not open file %s.\n",filename);
		return;
	}

	printf("Load Address: ");
	scanf("%lx", &loadaddr);

	/*								     	*/
	/*    See /usr/src/gnu/usr.bin/binutils/gdb/*srec* for info on Srec's 	*/	
	/*								     	*/
	do
	{
		stat = fgets(temp_line, MAXLINELEN, fd);
		srecordtype = (int)(temp_line[1] - '0');
	}
	while (stat && (srecordtype<1));

	while (stat && temp_line[0] == 'S' && srecordtype <= 5)
	{
		int n = 0;

		/* 			Grab S-record data payload 		     */
		/* We also hack it to convert it to lowercase hex chars if necessary */
		tptr = &temp_line[6+(2*srecordtype)];
		while (*tptr != '\r')
		{
			sprintf(&pkt[n++], "%x", hextoint(*tptr++));
		}

		/* Remove S-Record checksum */
		pkt[strlen(pkt)-2] = '\0';
	
		pktlen = strlen(pkt)/2;

		sprintf(buf, "M%lx,%x:%s", loadaddr, pktlen, pkt);
		fprintf(stderr, ".");

		if (send(buf, board) == OK)
		{
			if (receive(board) != OK)
			{
				fprintf(stderr,"Error while transmitting packet %d\n", npackets);
			}
			npackets++;
			loadaddr += pktlen; 
		}
		else
		{
			fprintf(stderr, "shload : %d Consecutive PACKET ERRORS\n", MAXPKTERROR);
			fprintf(stderr, "Aborting Load...\n");
			return;
		}

		stat = fgets(temp_line, MAXLINELEN, fd);
		srecordtype = (int)(temp_line[1] - '0');
	}

	free(pkt);
	free(response);

	return;
}

void
run(Board *board)
{
 	char buf[16];
	long execaddr;

	printf("Begin Execution At Address: ");
	scanf("%lx", &execaddr);

	sprintf(buf, "c%lx", execaddr);

	if (send(buf, board) != OK)
	{
		fprintf(stderr, "Could not send continue command to Board\n");
	}
	
	return;
}

void
regdump(Board *board)
{
	if(send("g", board) == OK)
	{
		if (receive(board) == OK)
		{
			fprintf(stderr, "Board replied with %s to regdump command\n", board->status);
		}
		else
		{
			fprintf(stderr, "Could not get reply to regdump command");
		}
	}
	else
	{
		fprintf(stderr, "Could not send register dump command to Board\n");
	}

	return;
}

void
memdump(Board *board)
{
	long start;
	char *buf;

	buf = (char *) malloc(sizeof(char)*32);

	fprintf(stdout, "Start Memory Address: ");
	scanf("8%lx", &start);
	
	snprintf(buf, 32, "m%lx,%x", start, MAXRESPONSELEN);

	if(send(buf, board) == OK)
	{
		if (receive(board) == OK)
		{
			fprintf(stderr, "Board replied with %s to regdump command\n", board->status);
		}
		else
		{
			fprintf(stderr, "Could not get reply to regdump command");
		}
	}
	else
	{
		fprintf(stderr, "Could not send register dump command to Board\n");
	}

	return;
}


void
reset(Board *board)
{
	if(send("r", board) == OK)
	{
		if (receive(board) == OK)
		{
			fprintf(stderr, "Board replied with %s to reset command\n", board->status);
		}
		else
		{
			fprintf(stderr, "Could not get reply to reset command");
		}	
	}
	else
	{
		fprintf(stderr, "Could not send reset command to Board\n");
	}

	return;
}

void
query_offsets(Board *board)
{
	if(send("qOffsets", board) == OK)
	{
		if (receive(board) == OK)
		{
			fprintf(stderr, "Board replied with %s to query offsets command\n", board->status);
		}
		else
		{
			fprintf(stderr, "Could not get reply to query offsets command");
		}	
	}
	else
	{
		fprintf(stderr, "Could not send query offsets command to Board\n");
	}

	return;
}

void
hint(Board *board)
{
	int 	i;
 	char 	readch;
	char 	writech;
	char	a,b,c;
	char	filename[MAXFILENAMELEN];
	FILE	*fd;

	printf("S-Record File: ");
	scanf("%64s", filename);
	
	fd = fopen(filename, "r");
	if (fd == NULL)
	{
		fprintf(stderr, "shload : ERROR - Could not open file %s.\n",filename);
		return;
	}

	fprintf(stdout, "\n\nPlease reset the board within the next 5 seconds");
	fflush(stdout);
	for (i = 0; i < 5; i++)
	{
		fprintf(stdout, ".");
		fflush(stdout);
		sleep(1);
	}

	writech = 'l';
	write(board->com, &writech, 1);
	writech = ' ';
	write(board->com, &writech, 1);
	writech = ':';
	write(board->com, &writech, 1);
	writech = ' ';
	write(board->com, &writech, 1);
	writech = 'x';				/* Hack : board does not need to know real name, just tell it 'x' */
	write(board->com, &writech, 1);
	writech = '\n';
	write(board->com, &writech, 1);


	while (1)
	{
		if (read(board->com, &readch, 1) != 0)
		{
			if (readch == ENQ)
			{
				fprintf(stdout,"\nGot an ENQ, attempting magic protocol...\n");

				writech = '*';
				write(board->com, &writech, 1);


				read(board->com, &a, 1);
				while (a != 'L')
				{
					read(board->com, &a, 1);
				}
				read(board->com, &b, 1);
				while (b != 'O')
				{
					read(board->com, &b, 1);
				}
				read(board->com, &c, 1);
				while (c != 'x')
				{
					read(board->com, &c, 1);
				}


				//fprintf(stdout, "Sent '*', got back [%c] [%c] [%c]\n", a, b, c);

				/*	Now, send SREC to CMON		*/
				while ((c = fgetc(fd)) != EOF)
				{
					write(board->com, &c, 1);

					/* wait till we get echo back */
					read(board->com, &readch, 1);
			
					while (readch != c)
					{
						if (readch == '>')
						{
							fclose(fd);

							writech = ACK;
							write(board->com, &writech, 1);

							fprintf(stderr, "\nDone loading. Running...\n\n\n");

							writech = '\n';
							write(board->com, &writech, 1);
							read(board->com, &readch, 1);
							while (readch != '>')
							{
								read(board->com, &readch, 1);
							}

							writech = 'g';
							write(board->com, &writech, 1);
							read(board->com, &readch, 1);
							while (readch != 'g')
							{
								read(board->com, &readch, 1);
							}

							writech = '\n';
							write(board->com, &writech, 1);
							read(board->com, &readch, 1);
							while (readch != '\n')
							{
								read(board->com, &readch, 1);
							}

							while (1)
							{
								if (read(board->com, &readch, 1) != 0)
								{
									fprintf(stdout,"%c", readch);
									fflush(stdout);
								}
							}

							return;
						}

						read(board->com, &readch, 1);
					}

					if (c == 'S') 
						fprintf(stderr, ".");
				}
			}
		}
	}

	return;
}

void
term(Board *board)
{
 	char 	readch;
	char 	writech;

	while (1)
	{
		if (read(board->com, &readch, 1) != 0)
		{
			fprintf(stdout,"%c", readch);
			fflush(stdout);
		}

		if (fgets(&writech, 1, stdin))
		{
			write(board->com, &writech, 1);
		}
	}

	return;
}
