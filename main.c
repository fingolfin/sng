#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include "png.h"
#include "sng.h"
#include "config.h"

int verbose;
int idat;

png_struct *png_ptr;
png_info *info_ptr;

/*************************************************************************
 *
 * Error and allocation functions
 *
 ************************************************************************/

int linenum;
char *file;
FILE *yyin;

void fatal(const char *fmt, ... )
/* throw an error distinguishable from PNG library errors */
{
    char buf[BUFSIZ];
    va_list ap;

    /* error message format can be stepped through by Emacs */
    if (!file)
	buf[0] = '\0';
    else if (linenum == EOF)
	sprintf(buf, "%s:EOF: ", file);
    else
	sprintf(buf, "%s:%d: ", file, linenum);

    va_start(ap, fmt);
    vsprintf(buf + strlen(buf), fmt, ap);
    va_end(ap);

    strcat(buf, "\n");
    fputs(buf, stderr);

    if (png_ptr)
	longjmp(png_jmpbuf(png_ptr), 2);
    else
	exit(2);
}

void *xalloc(unsigned long s)
{
    void *p=malloc((size_t)s);

    if (p==NULL) {
	fatal("out of memory");
    }

    return p;
}

void *xrealloc(void *p, unsigned long s)
{
    p=realloc(p,(size_t)s);

    if (p==NULL) {
	fatal("out of memory");
    }

    return p;
}

char *xstrdup(char *s)
{
    char	*r = xalloc(strlen(s) + 1);

    strcpy(r, s);

    return(r);
}

/*************************************************************************
 *
 * Hash initialization
 *
 ************************************************************************/

#undef HASHDEBUG

void initialize_hash(int hashfunc(color_item *), 
		     color_item *hashbuckets[],
		     int *initialized)
/* initialize color lookup by given hash function */
{
    if (!*initialized)
    {
	FILE	*fp;
	int red, green, blue;
	char line[BUFSIZ], namebuf[BUFSIZ];

	(*initialized)++;

	if ((fp = fopen(RGBTXT, "r")) == NULL)
	    fatal("RGB database %s is missing.", RGBTXT);
	else
	{
	    int st;
	    color_item sc;

	    for (;;)
	    {
		if (fgets(line, sizeof(line) - 1, fp) == NULL)
		    break;
		st = sscanf(line, "%d %d %d %[^\n]\n", 
			     &red, &green, &blue, namebuf);
		if (feof(fp))
		    break;
		else if (st == 4)
		{
		    color_item *op, *newcolor, **hashbucket;

#ifdef HASHDEBUG
		    printf("* Caching %s = (%u, %u, %u) => %d\n",
			   namebuf, red, green, blue, hashfunc(&sc));
#endif /* HASHDEBUG */
		    sc.r = (unsigned char)red;
		    sc.g = (unsigned char)green;
		    sc.b = (unsigned char)blue;
		    sc.name = namebuf;
		    hashbucket = &hashbuckets[hashfunc(&sc)];

		    newcolor  = xalloc(sizeof(color_item));
		    memcpy(newcolor, &sc, sizeof(color_item));
		    newcolor->name = xstrdup(namebuf);

		    op = *hashbucket;
		    *hashbucket = newcolor;
		    newcolor->next = op;
		}
	    }
	    fclose(fp);
	}
    }
}

/*************************************************************************
 *
 * Utility functions
 *
 ************************************************************************/

static int max(int x, int y)
{
    return x > y ? x : y;
}

int main(int argc, char *argv[])
{
    int i = 1;
    int error_status = 0;

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
	case 'i':    /* dump raw IDAT chunks - unimplemented, undocumented */
	    ++idat;
	    i++;
	    break;
	case 'V':
	    fprintf(stdout, "sng version " VERSION " by Eric S. Raymond.\n");
	    exit(0);
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
		error_status = max(error_status, 1);
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
		error_status = max(error_status, 1);
		continue;
	    }

	    if (verbose)
		printf("sng: converting %s to %s\n", argv[i], outfile);

	    if ((fpin = fopen(argv[i], "r")) == NULL)
	    {
		fprintf(stderr,
			"sng: couldn't open %s for input (%d)\n",
			argv[i], errno);
		error_status = max(error_status, 1);
		continue;
	    }
	    if ((fpout = fopen(outfile, "w")) == NULL)
	    {
		fprintf(stderr,
			"sng: couldn't open %s for output (%d)\n",
			outfile, errno);
		error_status = max(error_status, 1);
		continue;
	    }

	    if (sng2png)
		error_status = max(error_status, sngc(fpin, argv[i], fpout));
	    else
		error_status = max(error_status, sngd(fpin, argv[i], fpout));
	}
    }

    return error_status;
}
