/*****************************************************************************

NAME
   sngc.c -- compile SNG to PNG/MNG.

TODO
  * Test hex-mode data collection
  * Sanity checks
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

#define MEMORY_QUANTUM	1024

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
    {"pHYs",		FALSE,	0},
#define pCAL	18
    {"pHYs",		FALSE,	0},
#define sCAL	19
    {"pHYs",		FALSE,	0},
#define gIFg	20
    {"pHYs",		FALSE,	0},
#define gIFt	21
    {"pHYs",		FALSE,	0},
#define gIFx	22
    {"pHYs",		FALSE,	0},
#define fRAc	23
    {"pHYs",		FALSE,	0},

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
    if (*vp || result == 2147483647L)
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

static char *string_validate(bool token_ok)
/* validate current token as a string */
{
    double result;
    char *vp;

    if (!token_ok)
	fatal("EOF while expecting string constant");
    return(token_buffer);
}

static void collect_data(int pixperchar, int *pnbits, char **pbits)
/* collect data in either bitmap format */
{
    /*
     * A data segment consists of a byte stream. 
     * There are two possible formats:
     *
     * 1. One character per byte; values are
     * 0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ
     * up to 62 values per pixel.
     *
     * 2. Two hex digits per byte.
     *
     * In either format, whitespace is ignored.
     */
    char *bits = xalloc(MEMORY_QUANTUM);
    int quanta = 1;
    int	nbits = 0;
    int ocount = 0;
    int c;

    if (yydebug)
	fprintf(stderr, "collecting data in %s format\n", 
		pixperchar ? "pixel-per-character" : "hex");
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
    int chunktype;

    /* read IHDR data */
    info_ptr->bit_depth = 8;
    info_ptr->color_type = 0;
    info_ptr->interlace_type = PNG_INTERLACE_NONE;
    while (get_inner_token())
	if (token_equals("height"))
	    info_ptr->height = long_numeric(get_token());
	else if (token_equals("width"))
	    info_ptr->width = long_numeric(get_token());
	else if (token_equals("bitdepth"))	/* FIXME: range check */
	    info_ptr->bit_depth = byte_numeric(get_token());
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
}

static void compile_PLTE(void)
/* parse PLTE specification, set corresponding bits in info_ptr */
{
    png_color	palette[256];
    int ncolors;

    memset(palette, '\0', sizeof(palette));
    ncolors = 0;

    while (get_inner_token())
    {
	if (!token_equals("("))
	    fatal("bad syntax in PLTE description");
	palette[ncolors].red = byte_numeric(get_token());
	require_or_die(",");
	palette[ncolors].green = byte_numeric(get_token());
	require_or_die(",");
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
    collect_data(FALSE, &nbits, &bits);
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
	require_or_die(",");
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
    char *cp;

    get_token();	/* fill token buffer with string */
    slen = strlen(token_buffer);
    if (slen > 79)
	fatal("profile name too long");
    cp = token_buffer + strlen(token_buffer);
    *cp++ = 0;		/* null separator */
    *cp++ = 0;		/* compression method */
    if (!get_token() || !token_equals("}"))
	fatal("bad token `%s' in iCCP specification", token_buffer);

    /* FIXME: actually emit the chunk */
}

static void compile_sBIT(void)
/* compile an sBIT chunk, set corresponding bits in info_ptr */
{
    png_color_8	sigbits;
    bool color = (info_ptr->color_type & (PNG_COLOR_MASK_PALETTE | PNG_COLOR_MASK_COLOR));

    while (get_inner_token())
	if (token_equals("red"))
	{
	    if (!color)
		fatal("No color channels in this image type");
	    sigbits.red = byte_numeric(get_token());
	}
	else if (token_equals("green"))
	{
	    if (!color)
		fatal("No color channels in this image type");
	    sigbits.green = byte_numeric(get_token());
	}
	else if (token_equals("blue"))
	{
	    if (!color)
		fatal("No color channels in this image type");
	    sigbits.blue = byte_numeric(get_token());
	}
	else if (token_equals("gray"))
	{
	    if (color)
		fatal("No gray channel in this image type");
	    sigbits.gray = byte_numeric(get_token());
	}
	else if (token_equals("alpha"))
	{
	    if (info_ptr->color_type & PNG_COLOR_MASK_ALPHA)
		fatal("No alpha channel in this image type");
	    sigbits.alpha = byte_numeric(get_token());
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
	if (token_equals(","))
	    continue;
	else
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

    /* input sample size in bits */
    switch (info_ptr->color_type)
    {
    case PNG_COLOR_TYPE_GRAY:
	require_or_die("gray");
	tRNSbits.gray = short_numeric(get_token());
	break;

    case PNG_COLOR_TYPE_PALETTE:
	while (get_inner_token())
	    if (token_equals(","))
		continue;
	    else
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

static void compile_tEXt(void)
/* compile and emit an tEXt chunk */
{
    char	*keyword = (char *)NULL, *text = (char *)NULL;

    while (get_inner_token())
	if (token_equals("keyword"))
	    keyword = string_validate(get_token());
	else if (token_equals("text"))
	    text = string_validate(get_token());
	else
	    fatal("bad token `%s' in tEXt specification", token_buffer);

    if (!keyword || !text)
	fatal("keyword or text is mising");

    /* FIXME: actually emit the chunk */
}

static void compile_zTXt(void)
/* compile and emit an iTXt chunk */
{
    char	*keyword = (char *)NULL, *text = (char *)NULL;

    while (get_inner_token())
	if (token_equals("keyword"))
	    keyword = string_validate(get_token());
	else if (token_equals("text"))
	    text = string_validate(get_token());
	else
	    fatal("bad token `%s' in zTXt specification", token_buffer);

    if (!keyword || !text)
	fatal("keyword or text is mising");

    /* FIXME: actually emit the chunk */
}

static void compile_iTXt(void)
/* compile and emit an iTXt chunk */
{
    char	*language = (char *)NULL;
    char	*keyword = (char *)NULL;
    char	*text = (char *)NULL;

    while (get_inner_token())
	if (token_equals("language"))
	    language = string_validate(get_token());
	else if (token_equals("keyword"))
	    keyword = string_validate(get_token());
	else if (token_equals("text"))
	    text = string_validate(get_token());
	else
	    fatal("bad token `%s' in iTXt specification", token_buffer);

    if (!language || !keyword || !text)
	fatal("keyword or text is mising");

    /* FIXME: actually emit the chunk */
}

static void compile_IMAGE(void)
/* parse IMAGE specification and emit corresponding bits */
{
    png_byte	**rowpointers;
    int	i;

    /*
     * We know we can use format 1 if
     * (a) The image is paletted and the palette has 62 or fewer values.
     * (b) Bit depth is 4 or less.
     * These cover a lot of common cases.
     */
    bool	sample_per_char;
    int		nbits;
    char	*bits;
    int 	sample_size;

    /* input sample size in bits */
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

    /* can we fit a sample in one base-62 character? */
    sample_per_char =
	sample_size <= 5
	|| ((info_ptr->color_type & PNG_COLOR_MASK_PALETTE) 
	 			&& info_ptr->num_palette <= 62);

    /* collect the data */
    collect_data(sample_per_char, &nbits, &bits);

    if (nbits != info_ptr->width * info_ptr->height * (sample_size / 8))
	fatal("size of IMAGE doesn't match height * width in IHDR");

    /* FIXME: perhaps this should be optional? */
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
    int	prevchunk, errtype;
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
		png_write_info(png_ptr, info_ptr);
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

	case IMAGE:
	    if (properties[IDAT].count)
		fatal("can't mix IDAT and IMAGE specs");
	    /* force out the pre-IDAT portions */
	    if (properties[IMAGE].count == 0)
		png_write_info(png_ptr, info_ptr);
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

    /* It is REQUIRED to call this to finish writing the rest of the file */
    png_write_end(png_ptr, info_ptr);

    /* if you malloced the palette, free it here */
    free(info_ptr->palette);

    /* clean up after the write, and free any memory allocated */
    png_destroy_write_struct(&png_ptr, (png_infopp)NULL);

    return(0);
}

/* sngc.c ends here */
