#include <stdio.h>
#include "sng.h"

#define VERSION "0.1"

int verbose;
int sng_error;

static void process_file_pointer(FILE *fpin, char *name, FILE *fpout)
/* compile or decompile, depending on whether 1st char is printable */
{
    int	c = getchar();

    ungetc(c, stdin);

    if (isprint(c))
	sngc(fpin, name, fpout);
    else
	sngd(fpin, name, fpout);
}

static void usage(void)
/* issue usage/help message */
{
    fprintf(stderr, "SNG, version %s\n", VERSION);
    fprintf(stderr, "   by Eric S. Raymond.\n");
    fprintf(stderr,"Compile SNG files to PNG/MNG or decompile PNG to SNG\n\n");
    fprintf(stderr, "Usage:  sng [-v] [file...]\n");
}

int main(int argc, char *argv[])
{
    FILE *fp;
    int i = 1;

#ifdef __EMX__
    _wildcard(&argc, &argv);   /* Unix-like globbing for OS/2 and DOS */
#endif

    while(argc > 1 && argv[1][0] == '-') {
	switch(argv[1][i]) {
	case '\0':
	    argc--;
	    argv++;
	    i = 1;
	    break;
	case 'v':
	    ++verbose;
	    i++;
	    break;
	case 'h':
	default:
	    fprintf(stderr, "unknown option %c\n", argv[1][i]);
	    usage();
	    exit(1);
	}
    }

    if (argc == 1)
    {
	if (isatty(0))
	{ /* if stdin not redirected, give the user help */
	    usage();
	} else {
	    process_file_pointer(stdin, "stdin", stdout);
	}
    } else {
	for(i = 1; i < argc; i++) {
	    /* This is somewhat ugly.  It sets the file pointer to stdin if the
	     * filename is "-", otherwise it tries to open the given filename.
	     */
	    if ((fp = strcmp(argv[1],"-") == 0 ? stdin:fopen(argv[i],"rb")) == NULL) {
		perror(argv[i]);
		sng_error = 2;
	    } else {
		process_file_pointer(fp, fp == stdin? "stdin":argv[i], stdout);
		fclose(fp);
	    }
	}
    }

    /* This only returns the error on the last file.  Works OK if you are only
     * checking the status of a single file. */
    return sng_error;
}
