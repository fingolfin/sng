/*============================================================================
 *
 *   Copyright 1995-1999 by Alexander Lehmann <lehmann@usa.net>,
 *                          Andreas Dilger <adilger@enel.ucalgary.ca>,
 *                          Glenn Randers-Pehrson <randeg@alum.rpi.edu>,
 *                          Greg Roelofs <newt@pobox.com>,
 *                          John Bowler <jbowler@acm.org>,
 *                          Tom Lane <tgl@sss.pgh.pa.us>
 *                          Eric S. Raymond <esr@thyrsus.com>
 *  
 *   Permission to use, copy, modify, and distribute this software and its
 *   documentation for any purpose and without fee is hereby granted, provided
 *   that the above copyright notice appear in all copies and that both that
 *   copyright notice and this permission notice appear in supporting
 *   documentation.  This software is provided "as is" without express or
 *   implied warranty.
 *
 *===========================================================================*/

/*
 * Compilation example (GNU C, command line; replace "zlibpath" as appropriate):
 *
 *    without zlib:
 *       gcc -O -o pngcheck pngcheck.c
 *    with zlib support:
 *       gcc -O -DUSE_ZLIB -I/zlibpath -o pngcheck pngcheck.c -L/zlibpath -lz
 *    or (static zlib):
 *       gcc -O -DUSE_ZLIB -I/zlibpath -o pngcheck pngcheck.c /zlibpath/libz.a
 *
 * Windows compilation example (MSVC, command line, assuming VCVARS32.BAT or
 * whatever has been run; setargv.obj comes with MSVC and expands wildcards):
 *
 *    without zlib:
 *       cl -nologo -O -W3 -DWIN32 pngcheck.c setargv.obj
 *    with zlib support (note that Win32 zlib is compiled as a DLL by default):
 *       cl -nologo -O -W3 -DWIN32 -DUSE_ZLIB -I/zlibpath -c pngcheck.c
 *       link -nologo pngcheck.obj setargv.obj \zlibpath\zlib.lib
 *       [copy pngcheck.exe and zlib.dll to installation directory]
 *    or
 *       link -nologo pngcheck.obj setargv.obj \zlibpath\zlibstat.lib
 *       [if you have a static library for zlib]
 *
 * zlib info:
 *	ftp://www.cdrom.com/pub/infozip/zlib/zlib.html
 * PNG/MNG info:
 *	http://www.cdrom.com/pub/png/
 *	http://www.cdrom.com/pub/mng/
 *	ftp://swrinde.nde.swri.edu/pub/mng/
 * pngcheck sources:
 *	http://www.cdrom.com/pub/png/pngcode.html
 *	ftp://swrinde.nde.swri.edu/pub/png/applications/   (may be out of date)
 */

#include <stdio.h>
#include "sng.h"

#define VERSION "1.99.2 of 18 November 1999"

static void checklibs(void)
/* check to make sure we've got the shared libraries we expect */
{
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
}

static void process_file_pointer(FILE *fpin, char *name, FILE *fpout)
/* compile or decompile, depending on whether 1st char is printable */
{
    int	c = getchar();

    ungetc(c, stdin);

    if (isprint(c))
	sngc(fpin, name, fpout);
    else if (search)
	pngsearch(fpin, name, extract);
    else
	pngcheck(fpin, name, 0, NULL);
}

static void usage(void)
/* issue usage/help message */
{
    fprintf(stderr, "SNG, version %s\n", VERSION);
    fprintf(stderr, "   by Alexander Lehmann, Andreas Dilger, Greg Roelofs, and Eric S. Raymond.\n");
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
		error = 2;
	    } else {
		process_file_pointer(fp, fp == stdin? "stdin":argv[i], stdout);
		fclose(fp);
	    }
	}
    }

    /* This only returns the error on the last file.  Works OK if you are only
     * checking the status of a single file. */
    return error;
}
