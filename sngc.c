/*****************************************************************************

NAME
   sngc.c -- compile SNG to PNG/MNG.

TODO
  * Test hex-mode data collection
  * Test compilation of non-palette image files.
*****************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <stdarg.h>

/* public variables */
int linenum;
char *file;
int yydebug;


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

#ifndef PNG_KEYWORD_MAX_LENGTH
#define PNG_KEYWORD_MAX_LENGTH	79
#endif /* PNG_KEYWORD_MAX_LENGTH */

/*
 * Maximum string size -- the size of an IDAT buffer minus the minimum overhead
 * of a string chunk (that is, the overhead of a minimal tEXt chunk).  
 * That overhead: four characters of chunk name, plus zero characters of 
 * keyword, plus one character of NUL separator.
 */
#define PNG_STRING_MAX_LENGTH	(PNG_ZBUF_SIZE - 5)

#define MEMORY_QUANTUM	1024

#define MAX_TEXT_CHUNKS	64

#define PNG_MAX_LONG	2147483647L	/* 2^31 */

/* chunk types */
static chunkprops properties[] = 
{
/*
 * The PNG 1.0 chunks, listed in order of the summary table in section 4.3.
 * IEND is not listed here because it doesn't have to appear in the file.
 */
#define IHDR	0
    {"IHDR",		FALSE,	0},
#define PLTE	1
    {"PLTE",		FALSE,	0},
#define IDAT	2
    {"IDAT",		TRUE,	0},
#define cHRM	3
    {"cHRM",		FALSE,	0},
#define gAMA	4
    {"gAMA",		FALSE,	0},
#define iCCP	5
    {"iCCP",		FALSE,	0},
#define sBIT	6
    {"sBIT",		FALSE,	0},
#define sRGB	7
    {"sRGB",		FALSE,	0},
#define bKGD	8
    {"bKGD",		FALSE,	0},
#define hIST	9
    {"hIST",		FALSE,	0},
#define tRNS	10
    {"tRNS",		FALSE,	0},
#define pHYs	11
    {"pHYs",		FALSE,	0},
#define sPLT	12
    {"sPLT",		TRUE,	0},
#define tIME	13
    {"tIME",		FALSE,	0},
#define iTXt	14
    {"iTXt",		TRUE,	0},
#define tEXt	15
    {"tEXt",		TRUE,	0},
#define zTXt	16
    {"zTXt",		TRUE,	0},
/*
 * Special-purpose chunks in PNG Extensions 1.2.0 specification.
 */
#define oFFs	17
    {"oFFs",		FALSE,	0},
#define pCAL	18
    {"pCAL",		FALSE,	0},
#define sCAL	19
    {"sCAL",		FALSE,	0},
#define gIFg	20
    {"gIFg",		FALSE,	0},
#define gIFt	21
    {"gIFt",		FALSE,	0},
#define gIFx	22
    {"gIFx",		FALSE,	0},
#define fRAc	23
    {"fRAc",		FALSE,	0},

/*
 * Image pseudo-chunk
 */
#define IMAGE	24
    {"IMAGE",		FALSE,	0},

/*
 * Private chunks
 */
#define PRIVATE	25
    {"private",		TRUE,	0},
};

static png_struct *png_ptr;
static png_info *info_ptr;
static png_color palette[256];
static png_text text_chunks[MAX_TEXT_CHUNKS], *ptp;

static FILE *yyin;

/*************************************************************************
 *
 * Utility functions
 *
 ************************************************************************/

void fatal(const char *fmt, ... )
/* throw an error distinguishable from PNG library errors */
{
    char buf[BUFSIZ];
    va_list ap;

    /* error message format can be stepped through by Emacs */
    if (linenum == EOF)
	sprintf(buf, "%s:EOF: ", file);
    else
	sprintf(buf, "%s:%d: ", file, linenum);

    va_start(ap, fmt);
    vsprintf(buf + strlen(buf), fmt, ap);
    va_end(ap);

    strcat(buf, "\n");
    fputs(buf, stderr);

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

static char *xstrdup(char *s)
{
    char	*r = xalloc(strlen(s) + 1);

    strcpy(r, s);

    return(r);
}

/*************************************************************************
 *
 * Token-parsing code
 *
 ************************************************************************/

static char token_buffer[BUFSIZ];
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

    /*
     * Skip leading whitespace.
     *
     * Treat commas, colons, and semicolons as whitespace -- this is
     * a mildly sleazy way to ensure that the compiler has
     * *really* tolerant syntax...in case we ever decide to change
     * this, there are comments reading "comma" at every point
     * where we might actually want same.
     */
    for (;;)
    {
	w = fgetc(yyin);
	if (w == '\n')
	    linenum++;
	if (feof(yyin))
	    return(FALSE);
	else if (isspace(w) || w == ',' || w == ';' || w == ':')
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
		    ungetc(w, yyin);
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
	    else if (ispunct(c) && c != '.')
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

static int token_equals(const char *str)
/* does the currently fetched token equal a specified string? */
{
    return !strcmp(str, token_buffer);
}

static int get_inner_token(void)
/* get a token within a chunk specification */
{
    if (!get_token())
	fatal("unexpected EOF");
    else
	return(!token_equals("}"));	/* do we see end delimiter? */
}

static int push_token(void)
/* push back a token; must always be followed immediately by get_token */
{
    if (yydebug)
	fprintf(stderr, "pushing token: %s\n", token_buffer);
    pushed = TRUE;
}

static void require_or_die(const char *str)
/* croak if the next token doesn't match what we expect */
{
    if (!get_token())
	fatal("unexpected EOF");
    else if (!token_equals(str))
	fatal("unexpected token `%s'", token_buffer);
}

static png_uint_32 long_numeric(bool token_ok)
/* validate current token as a PNG long (range 0..2^31-1) */
{
    unsigned long result;
    char *vp;

    if (!token_ok)
	fatal("EOF while expecting long-integer constant");
    result = strtoul(token_buffer, &vp, 0);
    if (*vp || result >= PNG_MAX_LONG)
	fatal("invalid or out of range long constant `%s'", token_buffer);
    return(result);
}

static png_int_32 slong_numeric(bool token_ok)
/* validate current token as a signed PNG long (range 0..2^31-1) */
{
    long result;
    char *vp;

    if (!token_ok)
	fatal("EOF while expecting signed long-integer constant");
    result = strtol(token_buffer, &vp, 0);
    if (*vp || result >= PNG_MAX_LONG || result <= -PNG_MAX_LONG)
	fatal("invalid or out of range long constant `%s'", token_buffer);
    return(result);
}

static png_uint_16 short_numeric(bool token_ok)
/* validate current token as a PNG long (range 0..2^16-1) */
{
    unsigned long result;
    char *vp;

    if (!token_ok)
	fatal("EOF while expecting short-integer constant");
    result = strtoul(token_buffer, &vp, 0);
    if (*vp || result == 65536)
	fatal("invalid or out of range short constant `%s'", token_buffer);
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
	fatal("invalid or out of range byte constant `%s'", token_buffer);
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
	fatal("invalid or out of range double-precision constant `%s'", token_buffer);
    return(result);
}

static int string_validate(bool token_ok, char *stash)
/* validate current token as a string */
{
    if (!token_ok)
	fatal("EOF while expecting string constant");
    else
    {
	int	len = strlen(token_buffer);

	if (len > PNG_STRING_MAX_LENGTH)
	    fatal("string token is too long");
	strncpy(stash, token_buffer, PNG_STRING_MAX_LENGTH);
	return(len);
    }
}

static int keyword_validate(bool token_ok, char *stash)
/* validate current token as a PNG keyword */
{
    if (!token_ok)
	fatal("EOF while expecting PNG keyword");
    else
    {
	int	len = strlen(token_buffer);
	unsigned char	*cp;

	if (len > PNG_KEYWORD_MAX_LENGTH)
	    fatal("keyword token is too long");
	strncpy(stash, token_buffer, PNG_KEYWORD_MAX_LENGTH);
	if (isspace(stash[0]) || isspace(stash[len-1]))
	    fatal("keywords may not contain leading or trailing spaces");
	for (cp = stash; *cp; cp++)
	    if (*cp < 32 || (*cp > 126 && *cp < 161))
		fatal("keywords must contain Latin-1 characters only");
	    else if (isspace(cp[0]) && isspace(cp[1]))
		fatal("keywords may not contain consecutive spaces");
	return(len);
    }
}

static void collect_data(int *pnbits, char **pbits)
/* collect data in either bitmap format */
{
    /*
     * A data segment consists of a byte stream. 
     * There are presently two formats:
     *
     * base64: 
     *   One character per byte; values are
     * 0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ%$
     *
     * hex: 
     *   Two hex digits per byte.
     *
     * In either format, whitespace is ignored.
     */
    char *bits = xalloc(MEMORY_QUANTUM);
    int quanta = 1;
    int	nbits = 0;
    int ocount = 0;
    int c;
    bool pixperchar;

    if (!get_inner_token())
	fatal("missing format type in data segment");
    else if (token_equals("base64"))
	pixperchar = TRUE;
    else if (token_equals("hex"))
	pixperchar = FALSE;
    else
	fatal("unknown data format");

    while ((c = fgetc(yyin)))
	if (feof(yyin))
	    fatal("unexpected EOF in data segment");
	else if (c == '}')
	    break;
	else if (isspace(c))
	    continue;
	else 
        {
	    unsigned char	value;

	    if (nbits > quanta * MEMORY_QUANTUM)
		bits = xrealloc(bits, MEMORY_QUANTUM * ++quanta);

	    if (pixperchar)
	    {
		if (!isalpha(c) && !isdigit(c))
		    fatal("bad character %02x in IDAT block", c);
		else if (isdigit(c))
		    value = c - '0';
		else if (isupper(c))
		    value = (c - 'A') + 36;
		else 
		    value = (c - 'a') + 10;
		bits[nbits++] = value;
	    }
	    else
	    {
		if (!isxdigit(c))
		    fatal("bad character %02x in IDAT block", c);
		else if (isdigit(c))
		    value = c - '0';
		else if (isupper(c))
		    value = (c - 'A') + 10;
		else 
		    value = (c - 'a') + 10;
		if (ocount++ % 2)
		    bits[nbits] = value * 16;
		else
		    bits[nbits++] |= value;
	    }
	}

    *pnbits = nbits;
    *pbits = bits;
}

/*************************************************************************
 *
 * The compiler itself
 *
 ************************************************************************/

static void compile_IHDR(void)
/* parse IHDR specification, set corresponding bits in info_ptr */
{
    int chunktype, d;

    /* read IHDR data */
    info_ptr->bit_depth = 8;
    info_ptr->color_type = 0;
    info_ptr->interlace_type = PNG_INTERLACE_NONE;
    while (get_inner_token())
	if (token_equals("height"))
	    info_ptr->height = long_numeric(get_token());
	else if (token_equals("width"))
	    info_ptr->width = long_numeric(get_token());
	else if (token_equals("bitdepth"))
	    d = byte_numeric(get_token());
        else if (token_equals("using"))
	    continue;			/* `uses' is just syntactic sugar */
        else if (token_equals("palette"))
	    info_ptr->color_type |= PNG_COLOR_MASK_PALETTE;
        else if (token_equals("color"))
	    info_ptr->color_type |= PNG_COLOR_MASK_COLOR;
        else if (token_equals("alpha"))
	    info_ptr->color_type |= PNG_COLOR_MASK_ALPHA;
        else if (token_equals("with"))
	    continue;			/* `with' is just syntactic sugar */
        else if (token_equals("interlace"))
	    info_ptr->interlace_type = PNG_INTERLACE_ADAM7;
	else
	    fatal("bad token `%s' in IHDR specification", token_buffer);

    /* IHDR sanity checks */
    if (!info_ptr->height)
	fatal("image height is zero or nonexistent");
    else if (!info_ptr->width)
	fatal("image width is zero or nonexistent");
    else if (d != 1 && d != 2 && d != 4 && d != 8 && d != 16)
	fatal("illegal bit depth %d in IHDR", d);
    else if (info_ptr->color_type == PNG_COLOR_TYPE_PALETTE)
    {
	if (d > 8)
	    fatal("bit depth of paletted images must be 1, 2, 4, or 8");
    }
    else if (info_ptr->color_type != PNG_COLOR_TYPE_GRAY)
	fatal("bit depth of RGB- and alpha-using images must be 8 or 16");
    info_ptr->bit_depth = d;
}

static void compile_PLTE(void)
/* parse PLTE specification, set corresponding bits in info_ptr */
{
    int ncolors;

    memset(palette, '\0', sizeof(palette));
    ncolors = 0;

    while (get_inner_token())
    {
	if (!token_equals("("))
	    fatal("bad syntax in PLTE description");
	else if (ncolors >= 256)
	    fatal("too many palette entries in PLTE specification");
	palette[ncolors].red = byte_numeric(get_token());
	/* comma */
	palette[ncolors].green = byte_numeric(get_token());
	/* comma */
	palette[ncolors].blue = byte_numeric(get_token());
	require_or_die(")");
	ncolors++;
    }

    /* write out the accumulated palette entries */
    png_set_PLTE(png_ptr, info_ptr, palette, ncolors);
}

static void compile_IDAT(void)
/* parse IDAT specification and emit corresponding bits */
{
    int		nbits;
    char	*bits;

    /*
     * Collect raw hex data and write it out as a chunk.
     */
    collect_data(&nbits, &bits);
    png_write_chunk(png_ptr, "IDAT", bits, nbits);
    free(bits);
}

static void compile_cHRM(void)
/* parse cHRM specification, set corresponding bits in info_ptr */
{
    char	cmask = 0;

    while (get_inner_token())
    {
	float	*cvx = NULL, *cvy;

	if (token_equals("white"))
	{
	    cvx = &info_ptr->x_white;
	    cvy = &info_ptr->y_white;
	    cmask |= 0x01;
	}
	else if (token_equals("red"))
	{
	    cvx = &info_ptr->x_red;
	    cvy = &info_ptr->y_red;
	    cmask |= 0x02;
	}
	else if (token_equals("green"))
	{
	    cvx = &info_ptr->x_green;
	    cvy = &info_ptr->y_green;
	    cmask |= 0x04;
	}
	else if (token_equals("blue"))
	{
	    cvx = &info_ptr->x_blue;
	    cvy = &info_ptr->y_blue;
	    cmask |= 0x08;
	}
	else
	    fatal("invalid color `%s' name in cHRM specification",token_buffer);

	require_or_die("(");
	*cvx = double_numeric(get_inner_token());
	/* comma */
	*cvy = double_numeric(get_inner_token());
	require_or_die(")");
    }

    if (cmask != 0x0f)
	fatal("cHRM specification is not complete");
    else
	info_ptr->valid |= cHRM;
}

static void compile_iCCP(void)
/* compile and emit an iCCP chunk */
{
    int slen;
    char *cp, name[PNG_KEYWORD_MAX_LENGTH+1];

    slen = keyword_validate(get_token(), name);
    cp = token_buffer + strlen(token_buffer);
    *cp++ = 0;		/* null separator */
    *cp++ = 0;		/* compression method */
    if (!get_token() || !token_equals("}"))
	fatal("bad token `%s' in iCCP specification", token_buffer);

    /* FIXME: actually emit the chunk (can't be done with 1.0.5) */
}

static void compile_sBIT(void)
/* compile an sBIT chunk, set corresponding bits in info_ptr */
{
    png_color_8	sigbits;
    bool color = (info_ptr->color_type & (PNG_COLOR_MASK_PALETTE | PNG_COLOR_MASK_COLOR));
    int sample_depth = ((info_ptr->color_type & PNG_COLOR_MASK_PALETTE) ? 8 : info_ptr->bit_depth);

    while (get_inner_token())
	if (token_equals("red"))
	{
	    if (!color)
		fatal("No color channels in this image type");
	    sigbits.red = byte_numeric(get_token());
	    if (sigbits.red > sample_depth)
		fatal("red sample depth out of range");
	}
	else if (token_equals("green"))
	{
	    if (!color)
		fatal("No color channels in this image type");
	    sigbits.green = byte_numeric(get_token());
	    if (sigbits.green > sample_depth)
		fatal("red sample depth out of range");
	}
	else if (token_equals("blue"))
	{
	    if (!color)
		fatal("No color channels in this image type");
	    sigbits.blue = byte_numeric(get_token());
	    if (sigbits.blue > sample_depth)
		fatal("red sample depth out of range");
	}
	else if (token_equals("gray"))
	{
	    if (color)
		fatal("No gray channel in this image type");
	    sigbits.gray = byte_numeric(get_token());
	    if (sigbits.gray > sample_depth)
		fatal("gray sample depth out of range");
	}
	else if (token_equals("alpha"))
	{
	    if (info_ptr->color_type & PNG_COLOR_MASK_ALPHA)
		fatal("No alpha channel in this image type");
	    sigbits.alpha = byte_numeric(get_token());
	    if (sigbits.alpha > sample_depth)
		fatal("alpha sample depth out of range");
	}
	else 
	    fatal("invalid channel name `%s' in sBIT specification",
		  token_buffer);

    png_set_sBIT(png_ptr, info_ptr, &sigbits);
}

static void compile_bKGD(void)
/* compile a bKGD chunk, put data in info structure */
{
    png_color_16	bkgbits;

    while (get_inner_token())
	if (token_equals("red"))
	{
	    if (!(info_ptr->color_type & PNG_COLOR_MASK_COLOR))
		fatal("Can't use color background with this image type");
	    bkgbits.red = byte_numeric(get_token());
	}
	else if (token_equals("green"))
	{
	    if (!(info_ptr->color_type & PNG_COLOR_MASK_COLOR))
		fatal("Can't use color background with this image type");
	    bkgbits.green = short_numeric(get_token());
	}
	else if (token_equals("blue"))
	{
	    if (!(info_ptr->color_type & PNG_COLOR_MASK_COLOR))
		fatal("Can't use color background with this image type");
	    bkgbits.blue = short_numeric(get_token());
	}
	else if (token_equals("gray"))
	{
	    if (info_ptr->color_type & (PNG_COLOR_MASK_COLOR | PNG_COLOR_MASK_PALETTE))
		fatal("Can't use color background with this image type");
	    bkgbits.gray = short_numeric(get_token());
	}
	else if (token_equals("index"))
	{
	    if (!(info_ptr->color_type & PNG_COLOR_MASK_PALETTE))
		fatal("Can't use index background with a non-palette image");
	    bkgbits.index = byte_numeric(get_token());
	}
	else 
	    fatal("invalid channel `%s' name in bKGD specification", 
		  token_buffer);

    png_set_bKGD(png_ptr, info_ptr, &bkgbits);
}

static void compile_hIST(void)
/* compile a hIST chunk, put data in info structure */
{
    png_uint_16	hist[256];
    int		nhist = 0;

    while (get_inner_token())
	/* comma */
	hist[nhist++] = short_numeric(TRUE);

    if (nhist != info_ptr->num_palette)
	fatal("number of hIST values (%d) for palette doesn't match palette size (%d)", nhist, info_ptr->num_palette);

    png_set_hIST(png_ptr, info_ptr, hist);
}

static void compile_tRNS(void)
/* compile a tRNS chunk, put data in info structure */
{
    png_byte	trans[256];
    int		ntrans = 0;
    png_color_16	tRNSbits;

    memset(&tRNSbits, '0', sizeof(tRNSbits));

    switch (info_ptr->color_type)
    {
    case PNG_COLOR_TYPE_GRAY:
	require_or_die("gray");
	tRNSbits.gray = short_numeric(get_token());
	break;

    case PNG_COLOR_TYPE_PALETTE:
	while (get_inner_token())
	    /* comma */
	    trans[ntrans++] = byte_numeric(TRUE);
	break;

    case PNG_COLOR_TYPE_RGB:
	while (get_inner_token())
	    if (token_equals("red"))
		tRNSbits.red = byte_numeric(get_token());
	    else if (token_equals("green"))
		tRNSbits.green = short_numeric(get_token());
	    else if (token_equals("blue"))
		tRNSbits.blue = short_numeric(get_token());
	    else 
		fatal("invalid channel name `%s' in tRNS specification", 
		      token_buffer);
	break;

    case PNG_COLOR_TYPE_RGB_ALPHA:
    case PNG_COLOR_TYPE_GRAY_ALPHA:
	fatal("tRNS chunk not permitted with this image type");

    default:	/* should never happen */
	fatal("unknown color type");
    }

    png_set_tRNS(png_ptr, info_ptr, trans, ntrans, &tRNSbits);
}

static void compile_pHYs(void)
/* compile a pHYs chunk, put data in info structure */
{
    png_byte	unit = PNG_RESOLUTION_UNKNOWN;
    png_uint_32	res_x, res_y;

    while (get_inner_token())
	if (token_equals("resolution"))
	{
	    require_or_die("(");
	    res_x = long_numeric(get_token());
	    /* comma */
	    res_y = long_numeric(get_token());
	    require_or_die(")");
	}
	else if (token_equals("per"))
	    continue;
	else if (token_equals("meter"))
	    unit = PNG_RESOLUTION_METER;
	else
	    fatal("invalid token `%s' in pHYs", token_buffer);

    if (!res_x || !res_y)
	fatal("illegal or missing resolutions in pHYs specification");

    png_set_pHYs(png_ptr, info_ptr, res_x, res_y, unit);
}

/* these definitions belong in png.h */
typedef struct png_palette_16_struct
{
   png_uint_16 red;
   png_uint_16 green;
   png_uint_16 blue;
   png_uint_16 alpha;
   png_uint_16 frequency;
} png_palette_16;
typedef png_palette_16 FAR * png_palette_16p;
typedef png_palette_16 FAR * FAR * png_palette_16pp;

static void compile_sPLT(void)
/* compile sPLT chunk */
{
    char	keyword[PNG_KEYWORD_MAX_LENGTH+1];
    int		nkeyword = 0;
    png_byte	depth = 0;
    png_palette_16	spalette[256];
    int		nentries;

    while (get_inner_token())
	if (token_equals("name"))
	    nkeyword = keyword_validate(get_token(), keyword);
        else if (token_equals("depth"))
	{
	    depth = byte_numeric(get_token());
	    if (depth != 8 && depth != 16)
		fatal("invalid sample depth in sPLT");
	}
        else 
	    while (get_inner_token())
	    {
		if (!token_equals("("))
		    fatal("bad syntax in sPLT description");
		else if (nentries >= 256)
		    fatal("too many palette entries in sPLT specification");
		spalette[nentries].red = short_numeric(get_token());
		if (depth == 8 && spalette[nentries].red > 255)
		    fatal("red value too large for sample depth");
		/* comma */
		spalette[nentries].green = short_numeric(get_token());
		if (depth == 8 && spalette[nentries].green > 255)
		    fatal("green value too large for sample depth");
		/* comma */
		spalette[nentries].blue = short_numeric(get_token());
		if (depth == 8 && spalette[nentries].blue > 255)
		    fatal("blue value too large for sample depth");
		/* comma */
		spalette[nentries].alpha = short_numeric(get_token());
		if (depth == 8 && spalette[nentries].alpha > 255)
		    fatal("alpha value too large for sample depth");
		require_or_die(")");
		/* comma */
		spalette[nentries].frequency = short_numeric(get_token());
		nentries++;
	    }

    if (!nkeyword || !depth)
	fatal("incomplete sPLT specification");

    /* FIXME: make this call work */
    /* png_write_sPLT(png_ptr, keyword, depth, spalette, nentries); */
}

static void compile_tEXt(void)
/* compile a text chunk; queue it up to be emitted later */
{
    char	keyword[PNG_KEYWORD_MAX_LENGTH+1];
    char	text[PNG_STRING_MAX_LENGTH+1];
    int		nkeyword = 0, ntext = 0;

    while (get_inner_token())
	if (token_equals("keyword"))
	    nkeyword = keyword_validate(get_token(), keyword);
	else if (token_equals("text"))
	    ntext = string_validate(get_token(), text);
	else
	    fatal("bad token `%s' in tEXt specification", token_buffer);

    if (!nkeyword || !ntext)
	fatal("keyword or text is missing in tEXt specification");

    /*
     * If we've already written an IDAT chunk, then png_write_info()
     * has already ben called, so emit the text chunk directly.
     * Otherwise, queue it up for processing when png_write_info()
     * gets called.
     */
    if (properties[IDAT].count)
	png_write_tEXt(png_ptr, keyword, text, strlen(text));
    else if (ptp - text_chunks >= MAX_TEXT_CHUNKS)
	fatal("too many text chunks (limit is %d)", MAX_TEXT_CHUNKS);
    else
    {
	ptp->key = xstrdup(keyword);
	ptp->text = xstrdup(text);
	ptp->text_length = strlen(text);
	ptp->compression = PNG_TEXT_COMPRESSION_NONE;
    }
}

static void compile_zTXt(void)
/* compile and emit a zTXt chunk */
{
    char	keyword[PNG_KEYWORD_MAX_LENGTH+1];
    char	text[PNG_STRING_MAX_LENGTH+1];
    int		nkeyword = 0, ntext = 0;

    while (get_inner_token())
	if (token_equals("keyword"))
	    nkeyword = keyword_validate(get_token(), keyword);
	else if (token_equals("text"))
	    ntext = string_validate(get_token(), text);
	else
	    fatal("bad token `%s' in zTXt specification", token_buffer);

    if (!nkeyword || !ntext)
	fatal("keyword or text is missing in zTXt specification");

    /*
     * If we've already written an IDAT chunk, then png_write_info()
     * has already ben called, so emit the text chunk directly.
     * Otherwise, queue it up for processing when png_write_info()
     * gets called.
     */
    if (properties[IDAT].count)
	png_write_zTXt(png_ptr, keyword, text, strlen(text), PNG_TEXT_COMPRESSION_zTXt);
    else if (ptp - text_chunks >= MAX_TEXT_CHUNKS)
	fatal("too many text chunks (limit is %d)", MAX_TEXT_CHUNKS);
    else
    {
	ptp->key = xstrdup(keyword);
	ptp->text = xstrdup(text);
	ptp->text_length = strlen(text);
	ptp->compression = PNG_TEXT_COMPRESSION_zTXt;
    }
}

static void compile_iTXt(void)
/* compile and emit an iTXt chunk */
{
    char	language[PNG_KEYWORD_MAX_LENGTH+1];
    char	keyword[PNG_KEYWORD_MAX_LENGTH+1]; 
    char	text[PNG_STRING_MAX_LENGTH+1];
    int		nlanguage = 0, nkeyword = 0, ntext = 0;

    while (get_inner_token())
	if (token_equals("language"))
	    nlanguage = keyword_validate(get_token(), language);
	else if (token_equals("keyword"))
	    nkeyword = keyword_validate(get_token(), keyword);
	else if (token_equals("text"))
	    ntext = string_validate(get_token(), text);
	else
	    fatal("bad token `%s' in iTXt specification", token_buffer);

    if (!language || !keyword || !text)
	fatal("keyword or text is missing");

    /* FIXME: actually emit the chunk (can't be done with 1.0.5) */
}

static void compile_tIME(void)
/* compile a tIME chunk, put data in info structure */
{
    png_time stamp;
    int time_mask = 0;

    while (get_inner_token())
	if (token_equals("year"))
	{
	    stamp.year = short_numeric(get_token());
	    time_mask |= 0x01;
	}
	else if (token_equals("month"))
	{
	    stamp.month = byte_numeric(get_token());
	    if (stamp.month < 1 || stamp.month > 12)
		fatal("month value out of range");
	    time_mask |= 0x02;
	}
	else if (token_equals("day"))
	{
	    stamp.day = byte_numeric(get_token());
	    if (stamp.day < 1 || stamp.day > 31)
		fatal("day value out of range");
	    time_mask |= 0x04;
	}
	else if (token_equals("hour"))
	{
	    stamp.hour = byte_numeric(get_token());
	    if (stamp.hour > 23)
		fatal("hour value out of range");
	    time_mask |= 0x08;
	}
	else if (token_equals("minute"))
	{
	    stamp.minute = byte_numeric(get_token());
	    if (stamp.minute > 59)
		fatal("minute value out of range");
	    time_mask |= 0x10;
	}
	else if (token_equals("second"))
	{
	    stamp.second = byte_numeric(get_token());
	    if (stamp.second > 59)
		fatal("second value out of range");
	    time_mask |= 0x20;
	}
	else
	    fatal("bad token `%s' in tIME specification", token_buffer);

    if (time_mask != 0x3f)
	fatal("incomplete tIME specification");

    png_set_tIME(png_ptr, info_ptr, &stamp);
}

static void compile_oFFs(void)
/* parse oFFs specification and set corresponding info fields */
{
    png_byte	unit = PNG_OFFSET_PIXEL;	/* default to pixels */
    png_int_32	res_x, res_y;

    while (get_inner_token())
	if (token_equals("offset"))
	{
	    require_or_die("(");
	    res_x = slong_numeric(get_token());
	    /* comma */
	    res_y = slong_numeric(get_token());
	    require_or_die(")");
	}
	else if (token_equals("pixels"))
	    unit = PNG_OFFSET_PIXEL;
	else if (token_equals("micrometers"))
	    unit = PNG_OFFSET_MICROMETER;
	else
	    fatal("invalid token `%s' in oFFs", token_buffer);

    if (!res_x || !res_y)
	fatal("illegal or missing offsets in oFFs specification");

    png_set_oFFs(png_ptr, info_ptr, res_x, res_y, unit);
}

static void compile_IMAGE(void)
/* parse IMAGE specification and emit corresponding bits */
{
    png_byte	**rowpointers;
    int		i, nbits, sample_size;
    char	*bits;

    /* collect the data */
    collect_data(&nbits, &bits);

    /* compute input sample size in bits */
    switch (info_ptr->color_type)
    {
    case PNG_COLOR_TYPE_GRAY:
	sample_size = info_ptr->bit_depth;
	break;

    case PNG_COLOR_TYPE_PALETTE:
	sample_size = 8;
	break;

    case PNG_COLOR_TYPE_RGB:
	sample_size = info_ptr->bit_depth * 3;
	break;

    case PNG_COLOR_TYPE_RGB_ALPHA:
	sample_size = info_ptr->bit_depth * 4;
	break;

    case PNG_COLOR_TYPE_GRAY_ALPHA:
	sample_size = info_ptr->bit_depth * 2;
	break;

    default:	/* should never happen */
	fatal("unknown color type");
    }

    if (nbits != info_ptr->width * info_ptr->height * (sample_size / 8))
	fatal("size of IMAGE doesn't match height * width in IHDR");

    /* make image pack as small as possible */
    if (info_ptr->bit_depth<8)
	png_set_packing(png_ptr);

    /* got the bits; now write them out */
    rowpointers = (png_byte **)xalloc(sizeof(char *) * info_ptr->height);
    for (i = 0; i < info_ptr->height; i++)
	rowpointers[i] = &bits[i * info_ptr->width];
    png_write_image(png_ptr, rowpointers);
    free(bits);
}

int sngc(FILE *fin, FILE *fout)
/* compile SNG on fin to PNG on fout */
{
    int	prevchunk, errtype, i;
    float gamma;

    yyin = fin;
    ptp = text_chunks;

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

    /* interpret the following chunk specifications */
    prevchunk = NONE;
    while (get_token())
    {
	chunkprops *pp;

	for (pp = properties; 
		 pp < properties + sizeof(properties)/sizeof(chunkprops);
		 pp++)
	    if (token_equals(pp->name))
		goto ok;
	fatal("unknown chunk type `%s'", token_buffer);

    ok:
	if (!get_token())
	    fatal("unexpected EOF");
	if (!token_equals("{"))
	    fatal("missing chunk delimiter");
	if (!pp->multiple_ok && pp->count > 0)
	    fatal("illegal repeated chunk");

	switch (pp - properties)
	{
	case IHDR:
	    if (prevchunk != NONE)
		fatal("IHDR chunk must come first");
	    compile_IHDR();
	    break;

	case PLTE:
	    if (properties[IDAT].count)
		fatal("PLTE chunk must come before IDAT");
	    else if (properties[bKGD].count)
		fatal("PLTE chunk encountered after bKGD");
	    else if (properties[tRNS].count)
		fatal("PLTE chunk encountered after tRNS");
	    else if (!(info_ptr->color_type & PNG_COLOR_MASK_PALETTE))
		fatal("PLTE chunk specified for non-palette image type");
	    compile_PLTE();
	    break;

	case IDAT:
	    if (properties[IMAGE].count)
		fatal("can't mix IDAT and IMAGE specs");
	    if (prevchunk != IDAT && pp->count)
		fatal("IDAT chunks must be contiguous");
	    /* force out the pre-IDAT portions */
	    if (properties[IDAT].count == 0)
	    {
		png_set_text(png_ptr,info_ptr, text_chunks, ptp - text_chunks);
		png_write_info(png_ptr, info_ptr);
	    }
	    compile_IDAT();
	    break;

	case cHRM:
	    if (properties[PLTE].count || properties[IDAT].count)
		fatal("cHRM chunk must come before PLTE and IDAT");
	    compile_cHRM();
	    break;

	case gAMA:
	    if (properties[PLTE].count || properties[IDAT].count)
		fatal("gAMA chunk must come before PLTE and IDAT");
	    png_set_gAMA(png_ptr, info_ptr, double_numeric(get_token()));
	    if (!get_token() || !token_equals("}"))
		fatal("bad token `%s' in gAMA specification", token_buffer);
	    break;

	case iCCP:
	    if (properties[PLTE].count || properties[IDAT].count)
		fatal("iCCP chunk must come before PLTE and IDAT");
	    compile_iCCP();
	    break;

	case sBIT:
	    if (properties[PLTE].count || properties[IDAT].count)
		fatal("sBIT chunk must come before PLTE and IDAT");
	    compile_sBIT();
	    break;

	case sRGB:
	    if (properties[PLTE].count || properties[IDAT].count)
		fatal("sRGB chunk must come before PLTE and IDAT");
	    png_set_sRGB_gAMA_and_cHRM(png_ptr, info_ptr,
				       byte_numeric(get_token()));
	    if (!get_token() || !token_equals("}"))
		fatal("bad token `%s' in sRGB specification", token_buffer);
	    break;

	case bKGD:
	    if (properties[IDAT].count)
		fatal("bKGD chunk must come between PLTE (if any) and IDAT");
	    compile_bKGD();
	    break;

	case hIST:
	    if (!properties[PLTE].count || properties[IDAT].count)
		fatal("hIST chunk must come between PLTE and IDAT");
	    compile_hIST();
	    break;

	case tRNS:
	    if (properties[IDAT].count)
		fatal("tRNS chunk must come between PLTE (if any) and IDAT");
	    compile_tRNS();
	    break;

	case pHYs:
	    if (properties[IDAT].count)
		fatal("pHYs chunk must come before IDAT");
	    compile_pHYs();
	    break;

	case sPLT:
	    if (properties[IDAT].count)
		fatal("sPLT chunk must come before IDAT");
	    compile_sPLT();
	    break;

	case tIME:
	    compile_tIME();
	    break;

	case iTXt:
	    compile_iTXt();
	    break;

	case tEXt:
	    compile_tEXt();
	    break;

	case zTXt:
	    compile_zTXt();
	    break;

	case oFFs:
	    if (properties[IDAT].count)
		fatal("oFFs chunk must come before IDAT");
	    compile_oFFs();
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
	    fatal("fRAc chunk type is not defined yet");
	    break;

	case IMAGE:
	    if (properties[IDAT].count)
		fatal("can't mix IDAT and IMAGE specs");
	    /* force out the pre-IDAT portions */
	    if (properties[IMAGE].count == 0)
	    {
		png_set_text(png_ptr,info_ptr, text_chunks, ptp - text_chunks);
		png_write_info(png_ptr, info_ptr);
	    }
	    compile_IMAGE();
	    properties[IDAT].count++;
	    break;

	case PRIVATE:
	    fatal("FIXME: private chunk types are not handled yet");
	    break;
	}

	if (yydebug)
	    fprintf(stderr, "%s specification processed\n", pp->name);
	prevchunk = (pp - properties);
	pp->count++;
    }

    /* end-of-file sanity checks */
    linenum = EOF;
    if (!properties[PLTE].count && (info_ptr->color_type & PNG_COLOR_MASK_PALETTE))
	fatal("palette property set, but no PLTE chunk found");
    if (!properties[IDAT].count)
	fatal("no image data");
    if (properties[iCCP].count && properties[sRGB].count)
	fatal("cannot have both iCCP and sRGB chunks (PNG spec 4.2.2.4)");

    /* It is REQUIRED to call this to finish writing the rest of the file */
    png_write_end(png_ptr, info_ptr);

    /* if you malloced the palette, free it here */
    /* free(info_ptr->palette); */

    /* if we xstrdup()ed storage for any text chunks, free it now */
    for (i = 0; i < ptp - text_chunks; i++)
    {
	free(text_chunks[i].key);
	free(text_chunks[i].text);
    }

    /* clean up after the write, and free any memory allocated */
    png_destroy_write_struct(&png_ptr, (png_infopp)NULL);

    return(0);
}

/* sngc.c ends here */
