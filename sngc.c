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
/*
 * The PNG 1.0 chunks, listed in order of the summary table in section 4.3.
 * Neither IHDR nor IEND are listed here because neither chunk has to appear
 * in the file.
 */
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
/*
 * Special-purpose chunks in PNG 1.2 specification.
 */
#define oFFs	16
    {"pHYs",		FALSE,	0},
#define pCAL	17
    {"pHYs",		FALSE,	0},
#define sCAL	18
    {"pHYs",		FALSE,	0},
#define gIFg	19
    {"pHYs",		FALSE,	0},
#define gIFt	20
    {"pHYs",		FALSE,	0},
#define gIFx	21
    {"pHYs",		FALSE,	0},
#define fRAc	22
    {"pHYs",		FALSE,	0},
/*
 * Private chunks
 */
#define PRIVATE	23
    {"private",		TRUE,	0},
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
	if (yydebug)
	    fprintf(stderr, "saved token: %s\n", token_buffer);
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
	else if (isspace(w))		/* whitespace */
	    continue;
	else if (w == '#')		/* comment */
	{
	    for (;;)
	    {
		w = fgetc(yyin);
		if (feof(yyin))
		    return(FALSE);
		if (w == '\n')
		{
		    w = fgetc(yyin);
		    break;
		}
	    }
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
	    else if (tp >= token_buffer + sizeof(token_buffer))
		fatal("string token too long");
	    else
		*tp++ = c;
	}
    }
    else if (!ispunct(w))
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
	    else if (ispunct(c))
	    {
		ungetc(c, yyin);
		break;
	    }
	    else if (tp >= token_buffer + sizeof(token_buffer))
		fatal("token too long");
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
    if (yydebug)
	fprintf(stderr, "pushing token: %s\n", token_buffer);
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

static double double_numeric(bool token_ok)
/* validate current token as a double-precision value */
{
    double result;
    char *vp;

    if (!token_ok)
	fatal("EOF while expecting double-precision constant");
    result = strtod(token_buffer, &vp);
    if (*vp || result < 0)
	fatal("invalid or out of range double-precision constant");
    return(result);
}

/*************************************************************************
 *
 * The compiler itself
 *
 ************************************************************************/

static int get_chunktype(void)
/* expecting a chunk name as current token; return the chunk type index */
{
    chunkprops *pp;

    for (pp = properties; 
		 pp < properties + sizeof(properties)/sizeof(chunkprops);
		 pp++)
	if (token_equals(pp->name))
	    return(pp - properties);

    return(NONE);
}

static void compile_IHDR(void)
/* parse IHDR specification and emit corresponding bits */
{
    int chunktype;

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

    /* IHDR sanity checks & write */
    if (!info_ptr->height)
	fatal("image height is zero or nonexistent");
    else if (!info_ptr->width)
	fatal("image width is zero or nonexistent");
    else
    {
	/* write the required header */
	png_set_IHDR(png_ptr, info_ptr,
		 info_ptr->width,
		 info_ptr->height,
		 info_ptr->bit_depth,
		 info_ptr->color_type,
		 info_ptr->interlace_type,
		 PNG_COMPRESSION_TYPE_BASE,
		 PNG_FILTER_TYPE_BASE);
	if (yydebug)
	    fprintf(stderr, "IHDR processed\n");
    }
}

static void compile_PLTE(void)
/* parse PLTE specification and emit corresponding bits */
{
    png_color	palette[256];
    int ncolors;

    memset(palette, '\0', sizeof(palette));
    ncolors = 0;

    for(;;)
    {
	if (!get_token())
	    break;
	else if (!token_equals("("))
	{
	    push_token();
	    break;
	}
	palette[ncolors].red = byte_numeric(get_token());
	get_token();
	if (!token_equals(","))
	    fatal("bad syntax in palette specification");
	palette[ncolors].green = byte_numeric(get_token());
	get_token();
	if (!token_equals(","))
	    fatal("bad syntax in palette specification");
	palette[ncolors].blue = byte_numeric(get_token());
	get_token();
	if (!token_equals(")"))
	    fatal("bad syntax in palette specification");
	ncolors++;
    }

    /* write out the accumulated palette entries */
    if (yydebug)
	fprintf(stderr, "Wrote %d palette entries\n", ncolors);
    png_set_PLTE(png_ptr, info_ptr, palette, ncolors);
}

static int pngc(FILE *fin, FILE *fout)
/* compile PPNG on fin to PNG on fout */
{
    int	chunktype, prevchunk, errtype;
    float gamma;

    yyin = fin;

    /* Create and initialize the png_struct with the desired error handler
     * functions.  If you want to use the default stderr and longjump method,
     * you can supply NULL for the last three parameters.  We also check that
     * the library version is compatible with the one used at compile time,
     * in case we are using dynamically linked libraries.  REQUIRED.
     */
    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING,
				      (void *)NULL, NULL, NULL);

    if (png_ptr == NULL)
	return(2);

    /* Allocate/initialize the image information data.  REQUIRED */
    info_ptr = png_create_info_struct(png_ptr);
    if (info_ptr == NULL)
    {
	png_destroy_write_struct(&png_ptr,  (png_infopp)NULL);
	return(2);
    }

    /* if errtype is not 1, this was generated by fatal() */ 
    if ((errtype = setjmp(png_ptr->jmpbuf))) {
	if (errtype == 1)
	    fprintf(stderr, "%s:%d: libpng croaked\n", file, linenum);
	free(png_ptr);
	free(info_ptr);
	return 2-errtype;
    }

    /* set up the output control if you are using standard C streams */
    png_init_io(png_ptr, fout);

    /* handle initial header */
    compile_IHDR();

    /* now interpret the following chunk specifications */
    prevchunk = NONE;
    while (get_token() && (chunktype = get_chunktype()) > NONE)
    {
	chunkprops *pp = &properties[chunktype];

	if (!pp->multiple_ok && pp->count > 0)
	    fatal("illegal repeated chunk");

	switch (chunktype)
	{
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
	    compile_PLTE();
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
	    png_set_gAMA(png_ptr, info_ptr, double_numeric(get_token()));
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

	case oFFs:
	    if (properties[IDAT].count)
		fatal("oFFs chunk must come before IDAT");
	    fatal("FIXME: oFFs chunk type is not handled yet");
	    break;

	case pCAL:
	    if (properties[IDAT].count)
		fatal("pCAL chunk must come before IDAT");
	    fatal("FIXME: pCAL chunk type is not handled yet");
	    break;

	case sCAL:
	    if (properties[IDAT].count)
		fatal("sCAL chunk must come before IDAT");
	    fatal("FIXME: sCAL chunk type is not handled yet");
	    break;

	case gIFg:
	    fatal("FIXME: gIFg chunk type is not handled yet");
	    break;

	case gIFt:
	    fatal("FIXME: gIFt chunk type is not handled yet");
	    break;

	case gIFx:
	    fatal("FIXME: gIFx chunk type is not handled yet");
	    break;

	case fRAc:
	    fatal("FIXME: fRAc chunk type is not handled yet");
	    break;

	case PRIVATE:
	    fatal("FIXME: private chunk types are not handled yet");
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

    /* It is REQUIRED to call this to finish writing the rest of the file */
    png_write_end(png_ptr, info_ptr);

    /* if you malloced the palette, free it here */
    free(info_ptr->palette);

    /* clean up after the write, and free any memory allocated */
    png_destroy_write_struct(&png_ptr, (png_infopp)NULL);

    return(0);
}

int main(int argc, char *argv[])
{
    linenum = 0;
    file = "stdin";
    yydebug = 1;
    pngc(stdin, stdout);
}
