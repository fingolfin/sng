#include <errno.h>
#include <stdio.h>
#include "sng.h"
#include "config.h"

int verbose;
int sng_error;
#ifdef __UNUSED__
int idat;
#endif /* __UNUSED__ */

int main(int argc, char *argv[])
{
    FILE *fp;
    int i = 1;

#ifdef __EMX__
    _wildcard(&argc, &argv);   /* Unix-like globbing for OS/2 and DOS */
#endif

    while(argc > 1 && argv[1][0] == '-')
    {
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
#ifdef __UNUSED__
	case 'i':
	    ++idat;
	    i++;
	    break;
#endif /* __UNUSED__ */
	case 'h':
	default:
	    fprintf(stderr, "sng: unknown option %c\n", argv[1][i]);
	    exit(1);
	}
    }

    if (argc == 1)
    {
	if (isatty(0))
	    fprintf(stderr, "sng: usage sng [-v] [file...]\n");
	else
	{
	    int	c = getchar();

	    ungetc(c, stdin);

	    if (isprint(c))
		exit(sngc(stdin, "stdin", stdout));
	    else
		exit(sngd(stdin, "stdin", stdout));
	}
    } 
    else
    {
	for (i = 1; i < argc; i++)
	{
	    int sng2png, dot = strlen(argv[i]) - 4;
	    char outfile[BUFSIZ];
	    FILE	*fpin, *fpout;

	    if (argv[i][dot] != '.')
	    {
		fprintf(stderr, "sng: %s is neither SNG nor PNG\n", argv[i]);
		continue;
	    }
	    else if (strcmp(argv[i] + dot, ".sng") == 0)
	    {
		sng2png = TRUE;
		strncpy(outfile, argv[i], dot);
		outfile[dot] = '\0';
		strcat(outfile, ".png");
	    }
	    else if (strcmp(argv[i] + dot, ".png") == 0)
	    {
		sng2png = FALSE;
		strncpy(outfile, argv[i], dot);
		outfile[dot] = '\0';
		strcat(outfile, ".sng");
	    }
	    else
	    {
		fprintf(stderr, "sng: %s is neither SNG nor PNG\n", argv[i]);
		continue;
	    }

	    if (verbose)
		printf("sng: converting %s to %s\n", argv[i], outfile);

	    if ((fpin = fopen(argv[i], "r")) == NULL)
	    {
		fprintf(stderr,
			"sng: couldn't open %s for input (%d)\n",
			argv[i], errno);
		continue;
	    }
	    if ((fpout = fopen(outfile, "w")) == NULL)
	    {
		fprintf(stderr,
			"sng: couldn't open for output %s (%d)\n",
			outfile, errno);
		continue;
	    }

	    if (sng2png)
		sngc(fpin, argv[i], fpout);
	    else
		sngd(fpin, argv[i], fpout);
	}
    }

    /* This only returns the error on the last file.  Works OK if you are only
     * checking the status of a single file. */
    return sng_error;
}
