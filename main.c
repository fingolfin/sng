#include <stdio.h>
#include "sngc.h"
#include "pngcheck.h"

#define VERSION "1.99.2 of 18 November 1999"

int main(int argc, char *argv[])
{
    FILE *fp;
    int i = 1;

    /* set globals from the compiler side */
    linenum = 0;
    file = "stdin";
    yydebug = 0;

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
	    ++verbose;  /* verbose == 2 means decode IDATs and print filter info */
	    quiet=0;
	    i++;
	    break;
	case 'q':
	    verbose=0;
	    quiet=1;
	    i++;
	    break;
	case 't':
	    printtext=1;
	    i++;
	    break;
	case '7':
	    printtext=1;
	    sevenbit=1;
	    i++;
	    break;
	case 'p':
	    printpal=1;
	    i++;
	    break;
	case 'f':
	    force=1;
	    i++;
	    break;
	case 's':
	    search=1;
	    i++;
	    break;
	case 'x':
	    search=extract=1;
	    i++;
	    break;
	case 'h':
	    goto usage;
	    break;
	default:
	    fprintf(stderr, "unknown option %c\n", argv[1][i]);
	    error = 1;
	    goto usage;
	}
    }

    if (argc == 1)
    {
	if (isatty(0))
	{ /* if stdin not redirected, give the user help */
	usage:
	    fprintf(stderr, "PNGcheck, version %s\n", VERSION);
	    fprintf(stderr, "   by Alexander Lehmann, Andreas Dilger and Greg Roelofs.\n");
#ifdef USE_ZLIB
	    fprintf(stderr, "   Compiled with zlib %s; using zlib %s.\n",
		    ZLIB_VERSION, zlib_version);
#endif
	    fprintf(stderr, "\nTest PNG image files for corruption, and print size/type/compression info.\n\n");
	    fprintf(stderr, "Usage:  pngcheck [-vqt7f] file.png [file.png [...]]\n");
	    fprintf(stderr, "   or:  pngcheck [-vqt7f] file.mng [file.mng [...]]\n");
	    fprintf(stderr, "   or:  ... | pngcheck [-sx][vqt7f]\n");
	    fprintf(stderr, "   or:  pngcheck -{sx}[vqt7f] file-containing-PNGs...\n\n");
	    fprintf(stderr, "Options:\n");
	    fprintf(stderr, "   -v  test verbosely (print most chunk data)\n");
#ifdef USE_ZLIB
	    fprintf(stderr, "   -vv test very verbosely (decode & print line filters)\n");
#endif
	    fprintf(stderr, "   -q  test quietly (only output errors)\n");
	    fprintf(stderr, "   -t  print contents of tEXt chunks (can be used with -q)\n");
	    fprintf(stderr, "   -7  print contents of tEXt chunks, escape chars >=128 (for 7-bit terminals)\n");
	    fprintf(stderr, "   -p  print contents of PLTE, tRNS, hIST, sPLT and PPLT (can be used with -q)\n");
	    fprintf(stderr, "   -f  force continuation even after major errors\n");
	    fprintf(stderr, "   -s  search for PNGs within another file\n");
	    fprintf(stderr, "   -x  search for PNGs and extract them when found\n");
	    fprintf(stderr, "\nNote:  MNG support is incomplete.  Based on MNG Draft 64.\n");
	} else {
	    int	c;

	    if (search)
	    {
		pngsearch(stdin, "stdin", extract);
		return;
	    }

	    c = getchar();
	    ungetc(c, stdin);

	    if (!isprint(c))
		pngcheck(stdin, "stdin", 0, NULL);
	    else
		sngc(stdin, stdout);
	}
    } else {
#ifdef USE_ZLIB
	if (verbose > 1) {
	    /* make sure we're using the zlib version we were compiled to use */
	    if (zlib_version[0] != ZLIB_VERSION[0]) {
		fflush(stdout);
		fprintf(stderr, "zlib error:  incompatible version (expected %s,"
			" using %s):  ignoring -vv\n\n", ZLIB_VERSION, zlib_version);
		fflush(stderr);
		verbose = 1;
	    } else if (strcmp(zlib_version, ZLIB_VERSION) != 0) {
		fprintf(stderr, "zlib warning:  different version (expected %s,"
			" using %s)\n\n", ZLIB_VERSION, zlib_version);
		fflush(stderr);
	    }
	}
#endif /* USE_ZLIB */
	for(i = 1; i < argc; i++) {
	    /* This is somewhat ugly.  It sets the file pointer to stdin if the
	     * filename is "-", otherwise it tries to open the given filename.
	     */
	    if ((fp = strcmp(argv[1],"-") == 0 ? stdin:fopen(argv[i],"rb")) == NULL) {
		perror(argv[i]);
		error = 2;
	    } else {
		if (search)
		    pngsearch(fp, fp == stdin? "stdin":argv[i], extract);
		else
		    pngcheck(fp, fp == stdin? "stdin":argv[i], 0, NULL);
		fclose(fp);
	    }
	}
    }

    /* This only returns the error on the last file.  Works OK if you are only
     * checking the status of a single file. */
    return error;
}
