/*****************************************************************************

NAME
   pngc.c -- compile editable text PPNG to PNG.

DESCRIPTION
   This module compiles PPNG (Printable PNG) to PNG.

TODO
  * Chunk compilation
  * Sanity checks
*****************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>

#define PNG_INTERNAL
#include <png.h>

typedef int	bool;
#define FALSE	0
#define TRUE	1

#define NONE	-2

typedef struct {
    char	*name;		/* name of chunk type */
    bool	multiple_ok;	/* OK to have more than one? */
    int		count;		/* how many have we seen? */
} chunkprops;

/* chunk types */
static chunkprops properties[] = 
{
#define PLTE	0
    {"PLTE",		FALSE,	0},
#define IDAT	1
    {"IDAT",		TRUE,	0},
#define cHRM	2
    {"cHRM",		FALSE,	0},
#define gAMA	3
    {"gAMA",		FALSE,	0},
#define iCCP	4
    {"iCCP",		FALSE,	0},
#define sBIT	5
    {"sBIT",		FALSE,	0},
#define sRGB	6
    {"sRGB",		FALSE,	0},
#define bKGD	7
    {"bKGD",		FALSE,	0},
#define hIST	8
    {"hIST",		FALSE,	0},
#define tRNS	9
    {"tRNS",		FALSE,	0},
#define pHYs	10
    {"pHYs",		FALSE,	0},
#define sPLT	11
    {"sPLT",		TRUE,	0},
#define tIME	12
    {"tIME",		FALSE,	0},
#define iTXt	13
    {"iTXt",		TRUE,	0},
#define tEXt	14
    {"tEXt",		TRUE,	0},
#define zTXt	15
    {"zTXt",		TRUE,	0},
};

static png_struct *png_ptr;
static png_info *info_ptr;

static FILE *yyin;
static int linenum;
static char *file;
static int yydebug;

/*************************************************************************
 *
 * Utility functions
 *
 ************************************************************************/

void fatal(char *str)
/* throw an error distinguishable from PNG library errors */
{
    /* error message format can be stepped through by Emacs */
    if (linenum == EOF)
	fprintf(stderr, "%s:EOF: %s\n", file, str);
    else
	fprintf(stderr, "%s:%d: %s\n", file, linenum, str);
    if (png_ptr)
	longjmp(png_ptr->jmpbuf, 2);
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

/*************************************************************************
 *
 * Token-parsing code
 *
 ************************************************************************/

static char token_buffer[81];
static bool pushed;

static int get_token(void)
{
    char	w, c, *tp = token_buffer;

    if (pushed)
    {
	pushed = FALSE;
	return(TRUE);
    }

    /* skip leading whitespace */
    for (;;)
    {
	w = fgetc(yyin);
	if (w == '\n')
	    linenum++;
	if (feof(yyin))
	    return(FALSE);
	else if (isspace(c))		/* whitespace */
	    continue;
	else if (c == '#')		/* comment */
	{
	    w = fgetc(yyin);
	    if (feof(yyin))
		return(FALSE);
	    if (w == '\n')
		break;
	}
	else				/* non-space character */
	{
	    *tp++ = w;
	    break;
	}
    }

    /* accumulate token */
    if (w == '\'' || w == '"')
    {
	tp = token_buffer;
	for (;;)
	{
	    c = fgetc(yyin);
	    if (feof(yyin))
		return(FALSE);
	    else if (c == w)
		break;
	    else if (c == '\n')
		fatal("runaway string");
	    else
		*tp++ = c;
	}
    }
    else
	for (;;)
	{
	    c = fgetc(yyin);
	    if (feof(yyin))
		return(FALSE);
	    else if (isspace(c))
	    {
		if (c == '\n')
		    linenum++;
		break;
	    }
	    else
		*tp++ = c;
	}

    *tp = '\0';
    if (yydebug > 0)
	fprintf(stderr, "token: %s\n", token_buffer);
    return(TRUE);
}

static int push_token(void)
/* push back a token; must always be followed immediately by get_token */
{
    pushed = TRUE;
}

static int token_equals(char *str)
/* does the currently fetched token equal a specified string? */
{
    return !strcmp(str, token_buffer);
}

static png_uint_32 long_numeric(bool token_ok)
/* validate current token as a PNG long (range 0..2^31-1) */
{
    unsigned long result;
    char *vp;

    if (!token_ok)
	fatal("EOF while expecting long-integer constant");
    result = strtoul(token_buffer, &vp, 0);
    if (*vp || result == 2147483647L)
	fatal("invalid or out of range long constant");
    return(result);
}

static png_byte byte_numeric(bool token_ok)
/* validate current token as a byte */
{
    unsigned long result;
    char *vp;

    if (!token_ok)
	fatal("EOF while expecting byte constant");
    result = strtoul(token_buffer, &vp, 0);
    if (*vp || result > 255)
	fatal("invalid or out of range byte constant");
    return(result);
}

/*************************************************************************
 *
 * The compiler itself
 *
 ************************************************************************/

static int get_chunktype(void)
/* expect a chunk name as the next token; return the chunk type index */
{
    chunkprops *pp;

    /*
     * It's OK to hit end of file while looking for a chunk name.
     * In fact this is the *only* place it's OK...
     */
    if (!get_token())
	return(EOF);

    for (pp = properties; 
		 pp < properties + sizeof(properties)/sizeof(chunkprops);
		 pp++)
	if (token_equals(pp->name))
	    return(pp - properties);

    return(NONE);
}

static int pngc(FILE *fin, FILE *fout)
/* compile PPNG on fin to PNG on fout */
{
    int	chunktype, prevchunk, errtype;

    yyin = fin;

    png_ptr = xalloc(sizeof (png_struct));
    info_ptr = xalloc(sizeof (png_info));

    /* if errtype is not 1, this was generated by fatal() */ 
    if ((errtype = setjmp(png_ptr->jmpbuf))) {
	if (errtype == 1)
	    fprintf(stderr, "%s:%d: libpng croaked\n", file, linenum);
	free(png_ptr);
	free(info_ptr);
	return 2-errtype;
    }

    /* initialize PNG writing */
    png_write_init(png_ptr);
    png_info_init(info_ptr);
    png_init_io(png_ptr, fout);

    /* read IHDR data */
    info_ptr->bit_depth = 8;
    info_ptr->color_type = 0;
    info_ptr->interlace_type = PNG_INTERLACE_NONE;
    while (get_token())
	if (token_equals("height"))
	    info_ptr->height = long_numeric(get_token());
	else if (token_equals("width"))
	    info_ptr->width = long_numeric(get_token());
	else if (token_equals("bitdepth"))	/* FIXME: range check */
	    info_ptr->bit_depth = byte_numeric(get_token());
        else if (token_equals("uses"))
	    continue;		/* `uses' is just syntactic sugar */
        else if (token_equals("palette"))
	    info_ptr->color_type |= PNG_COLOR_MASK_PALETTE;
        else if (token_equals("color"))
	    info_ptr->color_type |= PNG_COLOR_MASK_COLOR;
        else if (token_equals("alpha"))
	    info_ptr->color_type |= PNG_COLOR_MASK_ALPHA;
        else if (token_equals("interlace"))
	    info_ptr->interlace_type = PNG_INTERLACE_ADAM7;
        else if ((chunktype = get_chunktype()) == EOF)
	    fatal("no image data after header");
	else if (chunktype == NONE)
	    fatal("bad token in IHDR block");
	else			/* we found a chunk name */
	{
	    push_token();
	    break;
	}

    /* IHDR sanity checks */
    if (!info_ptr->height)
	fatal("image height is zero or nonexistent");
    else if (!info_ptr->width)
	fatal("image width is zero or nonexistent");
    else if (yydebug)
	fprintf(stderr, "IHDR processed\n");

    /* now interpret the following chunk specifications */
    prevchunk = NONE;
    while ((chunktype = get_chunktype()) > NONE)
    {
	chunkprops *pp = &properties[chunktype];

	if (!pp->multiple_ok && pp->count > 0)
	    fatal("illegal repeated chunk");

	switch (chunktype)
	{
	case EOF:
	    /* all is well for normal termination */
	    break;

	case NONE:
	    fatal("unknown chunk type");
	    break;

	case PLTE:
	    if (properties[IDAT].count)
		fatal("PLTE chunk must come before IDAT");
	    else if (properties[bKGD].count)
		fatal("PLTE chunk encountered after bKGD");
	    else if (properties[tRNS].count)
		fatal("PLTE chunk encountered after tRNS");
	    fatal("FIXME: PLTE chunk type is not handled yet");
	    break;

	case IDAT:
	    if (prevchunk != IDAT && pp->count)
		fatal("IDAT chunks must be contiguous");
	    fatal("FIXME: IDAT chunk type is not handled yet");
	    break;

	case cHRM:
	    if (properties[PLTE].count || properties[IDAT].count)
		fatal("cHRM chunk must come before PLTE and IDAT");
	    fatal("FIXME: cHRM chunk type is not handled yet");
	    break;

	case gAMA:
	    if (properties[PLTE].count || properties[IDAT].count)
		fatal("gAMA chunk must come before PLTE and IDAT");
	    fatal("FIXME: gAMA chunk type is not handled yet");
	    break;

	case iCCP:
	    if (properties[PLTE].count || properties[IDAT].count)
		fatal("iCCP chunk must come before PLTE and IDAT");
	    fatal("FIXME: iCCP chunk type is not handled yet");
	    break;

	case sBIT:
	    if (properties[PLTE].count || properties[IDAT].count)
		fatal("sBIT chunk must come before PLTE and IDAT");
	    fatal("FIXME: sBIT chunk type is not handled yet");
	    break;

	case sRGB:
	    if (properties[PLTE].count || properties[IDAT].count)
		fatal("sRGB chunk must come before PLTE and IDAT");
	    fatal("FIXME: sRGB chunk type is not handled yet");
	    break;

	case bKGD:
	    if (properties[IDAT].count)
		fatal("bKGD chunk must come between PLTE (if any) and IDAT");
	    fatal("FIXME: bKGD chunk type is not handled yet");
	    break;

	case hIST:
	    if (!properties[PLTE].count || properties[IDAT].count)
		fatal("bKGD chunk must come between PLTE and IDAT");
	    fatal("FIXME: hIST chunk type is not handled yet");
	    break;

	case tRNS:
	    if (properties[IDAT].count)
		fatal("tRNS chunk must come between PLTE (if any) and IDAT");
	    fatal("FIXME: tRNS chunk type is not handled yet");
	    break;

	case pHYs:
	    if (properties[IDAT].count)
		fatal("pHYs chunk must come before IDAT");
	    fatal("FIXME: pHYs chunk type is not handled yet");
	    break;

	case sPLT:
	    if (properties[IDAT].count)
		fatal("sPLT chunk must come before IDAT");
	    fatal("FIXME: sPLT chunk type is not handled yet");
	    break;

	case tIME:
	    fatal("FIXME: tIME chunk type is not handled yet");
	    break;

	case iTXt:
	    fatal("FIXME: iTXt chunk type is not handled yet");
	    break;

	case tEXt:
	    fatal("FIXME: tEXt chunk type is not handled yet");
	    break;

	case zTXt:
	    fatal("FIXME: zTXt chunk type is not handled yet");
	    break;
	}

	prevchunk = chunktype;
	pp->count++;
    }

    /* end-of-file sanity checks */
    linenum = EOF;
    if (!properties[PLTE].count && (info_ptr->color_type & PNG_COLOR_MASK_PALETTE))
	fatal("palette property set, but no PLTE chunk found");
    if (!properties[IDAT].count)
	fatal("no image data");

    /* saw EOF, let's do a normal exit */
    png_write_end(png_ptr, info_ptr);
    png_write_destroy(png_ptr);

    free(png_ptr);
    free(info_ptr);
    return(0);
}

int main(int argc, char *argv[])
{
    linenum = 0;
    file = "stdin";
    yydebug = 1;
    pngc(stdin, stdout);
}
