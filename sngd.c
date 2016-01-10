/*****************************************************************************

NAME
   sngd.c -- decompile PNG to SNG.

*****************************************************************************/
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include "config.h"	/* for RGBTXT */
#include "png.h"
#include "sng.h"

static char *image_type[] = {
    "grayscale",
    "undefined type",
    "RGB",
    "colormap",
    "grayscale+alpha",
    "undefined type",
    "RGB+alpha"
};

static char *rendering_intent[] = {
  "perceptual",
  "relative colorimetric",
  "saturation-preserving",
  "absolute colorimetric"
};

static char *current_file;

/* Error status for the file being processed; reset to 0 at the top of sngd() */
static int sng_error;

/*****************************************************************************
 *
 * Interface to RGB database
 *
 *****************************************************************************/

#define COLOR_HASH(r, g, b)	(((r<<16)|(g<<8)|(b))%COLOR_HASH_MODULUS)

static color_item *rgb_hashbuckets[COLOR_HASH_MODULUS];
static int rgb_initialized;

static int hash_by_rgb(color_item *cp)
/* hash by color's RGB value */
{
    return(COLOR_HASH(cp->r, cp->g, cp->b));
}

static char *find_by_rgb(int r, int g, int b)
{
    color_item sc, *hp;

   sc.r = r; sc.g = g; sc.b = b;

    for (hp = rgb_hashbuckets[hash_by_rgb(&sc)]; hp; hp = hp->next)
	if (hp->r == r && hp->g == g && hp->b == b)
	    return(hp->name);

    return((char *)NULL);
}

/*****************************************************************************
 *
 * Low-level helper code
 *
 *****************************************************************************/

char *safeprint(const char *buf)
/* visibilize a given string -- inverse of sngc.c:escapes() */
{
    static char vbuf[PNG_STRING_MAX_LENGTH*4+1];
    char *tp = vbuf;

    while (*buf)
    {
	if (*buf == '"')
	{
	    *tp++ = '\\'; *tp++ = '"';
	    buf++;
	}
	else if (*buf == '\\')
	{
	    *tp++ = '\\'; *tp++ = '\\';
	    buf++;
	}
	else if (isprint(*buf) || *buf == ' ')
	    *tp++ = *buf++;
	else if (*buf == '\n')
	{
	    *tp++ = '\\'; *tp++ = 'n';
	    buf++;
	}
	else if (*buf == '\r')
	{
	    *tp++ = '\\'; *tp++ = 'r';
	    buf++;
	}
	else if (*buf == '\b')
	{
	    *tp++ = '\\'; *tp++ = 'b';
	    buf++;
	}
	else if ((unsigned char) *buf < ' ')
	{
	    *tp++ = '\\'; *tp++ = '^'; *tp++ = '@' + *buf;
	    buf++;
	}
	else
	{
	    (void) sprintf(tp, "\\x%02x", (unsigned char) *buf++);
	    tp += strlen(tp);
	}
    }
    *tp++ = '\0';
    return(vbuf);
}

static void multi_dump(FILE *fpout, char *leader,
		       int width, int height,
		       unsigned char *data[])
/* dump data in a recompilable form */
{
    unsigned char *cp;
    int i, all_printable = 1, base64 = 1;
    png_byte	bit_depth = png_get_bit_depth(png_ptr, info_ptr);
    png_byte	channels = png_get_channels(png_ptr, info_ptr);

#define SHORT_DATA	50

    for (i = 0; i < height; i++)
	for (cp = data[i]; cp < data[i] + width; cp++)
	{
	    if (!isprint(*cp) && !isspace(*cp))
		all_printable = 0;
	    if (*cp >= 64)
		base64 = 0;
	}

    for (i = 0; i < height; i++)
    {
	if (all_printable)
	{
	    unsigned char *cp;

	    if (i == 0)
	    {
		fprintf(fpout, "%s ", leader);
		if (height == 1 && width < SHORT_DATA)
		    fprintf(fpout, " ");
		else
		    fprintf(fpout, "\n");
	    }

	    fputc('"', fpout);
	    for (cp = data[i]; cp < data[i] + width; cp++)
	    {
		char	cbuf[2];

		cbuf[0] = *cp;
		cbuf[1] = '\0';
		fputs(safeprint(cbuf), fpout); 

		if (*cp == '\n' && cp < data[i] + width - 1)
		    fprintf(fpout, "\"\n\"");
	    }
	    fprintf(fpout, "\"%c\n", height == 1 ? ';' : ' ');
	}
	else if (base64)
	{
	    if (i == 0)
	    {
		fprintf(fpout, "%sbase64", leader);
		if (height == 1 && width < SHORT_DATA)
		    fprintf(fpout, " ");
		else
		    fprintf(fpout, "\n");
	    }
	    for (cp = data[i]; cp < data[i] + width; cp++) {
	        if (*cp >= 64)
		   fatal("invalid base64 data (%d)", *cp);
		fputc(BASE64[*cp], fpout);
	    }
	    if (height == 1)
		fprintf(fpout, ";\n");
	    else
		fprintf(fpout, "\n");
	}
	else
	{
	    if (i == 0)
	    {
		fprintf(fpout, "%shex", leader);
		if (height == 1 && width < SHORT_DATA)
		    fprintf(fpout, " ");
		else
		    fprintf(fpout, "\n");
	    }
	    for (cp = data[i]; cp < data[i] + width; cp++)
	    {
		fprintf(fpout, "%02x", *cp & 0xff);

		/* only insert spacers for 8-bit images if > 1 channel */
		if (bit_depth == 8 && channels > 1)
		{
		    if (((cp - data[i]) % channels) == channels - 1)
			fputc(' ', fpout);
		}
		else if (bit_depth == 16)
		    if (((cp - data[i]) % (channels*2)) == channels*2-1)
			fputc(' ', fpout);
	    }
	    if (height == 1)
		fprintf(fpout, ";\n");
	    else
		fprintf(fpout, "\n");
	}
    }
}

static void dump_data(FILE *fpout, char *leader, int size, unsigned char *data)
{
    unsigned char *dope[1];

    dope[0] = data;
    multi_dump(fpout, leader, size, 1, dope);
}

static void printerr(int err, const char *fmt, ... )
/* throw an error distinguishable from PNG library errors */
{
    char buf[BUFSIZ];
    va_list ap;

    sprintf(buf, "sng: in %s, ", current_file);

    va_start(ap, fmt);
    vsprintf(buf + strlen(buf), fmt, ap);
    va_end(ap);

    strcat(buf, "\n");
    fputs(buf, stderr);

    sng_error = err;
}

/*****************************************************************************
 *
 * Chunk handlers
 *
 *****************************************************************************/

static void dump_IHDR(FILE *fpout)
{
    png_uint_32 width;
    png_uint_32 height;
    int bit_depth;
    int ityp;
    int interlace_type;

    png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &ityp, &interlace_type, 0, 0);

    if (width == 0 || height == 0) {
	printerr(1, "invalid IHDR image dimensions (%lux%lu)",
		 width, height);
    }

    if (ityp > sizeof(image_type)/sizeof(char*)) {
	ityp = 1; /* avoid out of range array index */
    }
    switch (bit_depth) {
    case 1:
    case 2:
    case 4:
	if (ityp == 2 || ityp == 4 || ityp == 6) {/* RGB or GA or RGBA */
	    printerr(1, "invalid IHDR bit depth (%u) for %s image",
		     bit_depth, image_type[ityp]);
	}
	break;
    case 8:
	break;
    case 16:
	if (ityp == 3) { /* palette */
	    printerr(1, "invalid IHDR bit depth (%u) for %s image",
		     bit_depth, image_type[ityp]);
	}
	break;
    default:
	printerr(1, "invalid IHDR bit depth (%u)", bit_depth);
	break;
    }

    fprintf(fpout, "IHDR {\n");
    fprintf(fpout, "    width: %u; height: %u; bitdepth: %u;\n", 
	    width, height, bit_depth);
    fprintf(fpout, "    using");
    if (ityp & PNG_COLOR_MASK_COLOR)
	fprintf(fpout, " color");
    else
	fprintf(fpout, " grayscale");
    if (ityp & PNG_COLOR_MASK_PALETTE)
	fprintf(fpout, " palette");
    if (ityp & PNG_COLOR_MASK_ALPHA)
	fprintf(fpout, " alpha");
    fprintf(fpout, ";\n");
    if (interlace_type)
	fprintf(fpout, "    with interlace;        # type adam7 assumed\n");
    fprintf(fpout, "}\n");
}

static void dump_PLTE(FILE *fpout)
{
    int i;
    png_byte	color_type = png_get_color_type(png_ptr, info_ptr);
    png_colorp	palette;
    int		num_palette;

    png_get_PLTE(png_ptr, info_ptr, &palette, &num_palette);

    initialize_hash(hash_by_rgb, rgb_hashbuckets, &rgb_initialized);

    if (color_type & PNG_COLOR_MASK_PALETTE)
    {
	fprintf(fpout, "PLTE {\n");
	for (i = 0;  i < num_palette;  i++)
	{
	    char	*name = NULL;

	    fprintf(fpout, 
		    "    (%3u,%3u,%3u)     # rgb = (0x%02x,0x%02x,0x%02x)",
		    palette[i].red,
		    palette[i].green,
		    palette[i].blue,
		    palette[i].red,
		    palette[i].green,
		    palette[i].blue);

	    if (rgb_initialized)
		name = find_by_rgb(palette[i].red,
				   palette[i].green,
				   palette[i].blue);
	    if (name)
		fprintf(fpout, " %s", name);
	    fputc('\n', fpout);
	}

	fprintf(fpout, "}\n");
    }
}

static void dump_image(png_bytepp rows, FILE *fpout)
{
    if (idat)
    {
	int	i;

	for (i = 0; i < idat; i++)
	{
	    fprintf(fpout, "IDAT {\n");
	    fprintf(fpout, "}\n");
	}
    }
    else
    {
	fprintf(fpout, "IMAGE {\n");
	multi_dump(fpout, "    pixels ", 
		   png_get_rowbytes(png_ptr, info_ptr),  png_get_image_height(png_ptr, info_ptr),
		   rows);
	fprintf(fpout, "}\n");
    }
}

static void dump_bKGD(FILE *fpout)
{
    png_color_16p	background;
    if (png_get_bKGD(png_ptr, info_ptr, &background))
    {
        png_byte	color_type = png_get_color_type(png_ptr, info_ptr);

	fprintf(fpout, "bKGD {");
	switch (color_type) {
	case PNG_COLOR_TYPE_GRAY:
	case PNG_COLOR_TYPE_GRAY_ALPHA:
	    fprintf(fpout, "gray: %u;", background->gray);
	    break;
	case PNG_COLOR_TYPE_RGB:
	case PNG_COLOR_TYPE_RGB_ALPHA:
	    fprintf(fpout, "red: %u;  green: %u;  blue: %u;",
			background->red,
			background->green,
			background->blue);
	    break;
	case PNG_COLOR_TYPE_PALETTE:
	    fprintf(fpout, "index: %u", background->index);
	    break;
	default:
	    printerr(1, "unknown image type");
	}
	fprintf(fpout, "}\n");
    }
}

static void dump_cHRM(FILE *fpout)
{
    double wx, wy, rx, ry, gx, gy, bx, by;

    if (!png_get_valid(png_ptr, info_ptr, PNG_INFO_cHRM))
	return;

#ifdef PNG_FLOATING_POINT_SUPPORTED
    png_get_cHRM(png_ptr, info_ptr, &wx, &wy, &rx, &ry, &gx, &gy, &bx, &by);
#else
#ifdef PNG_FIXED_POINT_SUPPORTED
    png_fixed_point wx_f, wy_f, rx_f, ry_f, gx_f, gy_f, bx_f, by_f;
    png_get_cHRM_fixed(png_ptr, info_ptr, &wx_f, &wy_f, &rx_f, &ry_f, &gx_f, &gy_f, &bx_f, &by_f);
    wx = FIXED_TO_FLOAT(wx_f);
    wy = FIXED_TO_FLOAT(wy_f);
    rx = FIXED_TO_FLOAT(rx_f);
    ry = FIXED_TO_FLOAT(ry_f);
    gx = FIXED_TO_FLOAT(gx_f);
    gy = FIXED_TO_FLOAT(gy_f);
    bx = FIXED_TO_FLOAT(bx_f);
    by = FIXED_TO_FLOAT(by_f);
#endif
#endif

    if (wx < 0 || wx > 0.8 || wy < 0 || wy > 0.8 || wx + wy > 1.0) {
	printerr(1, "invalid cHRM white point %0g %0g", wx, wy);
    } else if (rx < 0 || rx > 0.8 || ry < 0 || ry > 0.8 || rx + ry > 1.0) {
	printerr(1, "invalid cHRM red point %0g %0g", rx, ry);
    } else if (gx < 0 || gx > 0.8 || gy < 0 || gy > 0.8 || gx + gy > 1.0) {
	printerr(1, "invalid cHRM green point %0g %0g", gx, gy);
    } else if (bx < 0 || bx > 0.8 || by < 0 || by > 0.8 || bx + by > 1.0) {
	printerr(1, "invalid cHRM blue point %0g %0g", bx, by);
    }

    fprintf(fpout, "cHRM {\n");
    fprintf(fpout, "    white: (%0g, %0g);\n", wx, wy);
    fprintf(fpout, "    red:   (%0g, %0g);\n", rx, ry);
    fprintf(fpout, "    green: (%0g, %0g);\n", gx, gy);
    fprintf(fpout, "    blue:  (%0g, %0g);\n", bx, by);
    fprintf(fpout, "}\n");
}

static void dump_gAMA(FILE *fpout)
{
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_gAMA)) {
#ifdef PNG_FLOATING_POINT_SUPPORTED
        double gamma;
        png_get_gAMA(png_ptr, info_ptr, &gamma);
        fprintf(fpout, "gAMA {%#0.5g}\n", gamma);
#else
#ifdef PNG_FIXED_POINT_SUPPORTED
        png_fixed_point int_gamma;
        png_get_gAMA_fixed(png_ptr, info_ptr, &int_gamma);
        fprintf(fpout, "gAMA {%#0.5g}\n", FIXED_TO_FLOAT(int_gamma));
#endif
#endif
    }
}

static void dump_hIST(FILE *fpout)
{
    png_uint_16p	hist;

    if (png_get_hIST(png_ptr, info_ptr, &hist)) {
	int	j;
	png_colorp	palette;
	int		num_palette;

	png_get_PLTE(png_ptr, info_ptr, &palette, &num_palette);

	fprintf(fpout, "hIST {\n");
	fprintf(fpout, "   ");
	for (j = 0; j < num_palette;  j++)
	    fprintf(fpout, " %3u", hist[j]);
	fprintf(fpout, ";\n}\n");
    }
}

static void dump_iCCP(FILE *fpout)
{
    png_charp name;
    int compression_type;
#if PNG_LIBPNG_VER_MAJOR < 1 || (PNG_LIBPNG_VER_MAJOR == 1 && PNG_LIBPNG_VER_MINOR <= 4)
    png_charp profile;
#else
    png_bytep profile;
#endif
    png_uint_32 proflen;

    if (png_get_iCCP(png_ptr, info_ptr, &name, &compression_type, &profile, &proflen)) {
	fprintf(fpout, "iCCP {\n");
	fprintf(fpout, "    name: \"%s\"\n", safeprint(name));
	dump_data(fpout, "    profile: ", proflen, profile);
	fprintf(fpout, "}\n");
    }
}

static void dump_oFFs(FILE *fpout)
{
    png_int_32 offset_x;
    png_int_32 offset_y;
    int unit_type;

    if (png_get_oFFs(png_ptr, info_ptr, &offset_x, &offset_y, &unit_type)) {
	fprintf(fpout, "oFFs {xoffset: %d; yoffset: %d;",
	       offset_x, offset_y);
	if (unit_type == PNG_OFFSET_PIXEL)
	    fprintf(fpout, " unit: pixels");
	else if (unit_type == PNG_OFFSET_MICROMETER)
	    fprintf(fpout, " unit: micrometers");
	fprintf(fpout, ";}\n");
    }
}
static void dump_pHYs(FILE *fpout)
{
    png_uint_32 res_x;
    png_uint_32 res_y;
    int unit_type;

    if (png_get_pHYs(png_ptr, info_ptr, &res_x, &res_y, &unit_type)) {
	if (unit_type > 1)
	    printerr(1, "invalid pHYs unit");
	else {
	    fprintf(fpout, "pHYs {xpixels: %u; ypixels: %u;",
		   res_x, res_y);
	    if (unit_type == PNG_RESOLUTION_METER)
		fprintf(fpout, " per: meter;");
	    fprintf(fpout, "}");
	    if (unit_type == 1 && res_x == res_y)
		fprintf(fpout, "  # (%lu dpi)\n", (long)(res_x*0.0254 + 0.5));
	    else
		fputc('\n', fpout);
	}
    }
}

static void dump_sBIT(FILE *fpout)
{
    png_byte color_type = png_get_color_type(png_ptr, info_ptr);
    png_byte bit_depth = png_get_bit_depth(png_ptr, info_ptr);
    png_color_8p sig_bit;
    int	maxbits = (color_type == 3)? 8 : bit_depth;

    if (png_get_sBIT(png_ptr, info_ptr, &sig_bit)) {
	fprintf(fpout, "sBIT {\n");
	switch (color_type) {
	case PNG_COLOR_TYPE_GRAY:
	    if (sig_bit->gray == 0 || sig_bit->gray > maxbits) {
		printerr(1, "%u sBIT gray bits not valid for %ubit/sample image",
			 sig_bit->gray, maxbits);
	    } else {
		fprintf(fpout, "    gray: %u;\n", sig_bit->gray);
	    }
	    break;
	case PNG_COLOR_TYPE_RGB:
	case PNG_COLOR_TYPE_PALETTE:
	    if (sig_bit->red == 0 || sig_bit->red > maxbits) {
		printerr(1, "%u sBIT red bits not valid for %ubit/sample image",
			 sig_bit->red, maxbits);
	    } else if (sig_bit->green == 0 || sig_bit->green > maxbits) {
		printerr(1, "%u sBIT green bits not valid for %ubit/sample image",
			 sig_bit->green, maxbits);
	    } else if (sig_bit->blue == 0 || sig_bit->blue > maxbits) {
		printerr(1, "%u sBIT blue bits not valid for %ubit/sample image",
			 sig_bit->blue, maxbits);
	    } else {
		fprintf(fpout, "    red: %u; green: %u; blue: %u;\n",
			sig_bit->red, sig_bit->green, sig_bit->blue);
	    }
	    break;
	case PNG_COLOR_TYPE_GRAY_ALPHA:
	    if (sig_bit->gray == 0 || sig_bit->gray > maxbits) {
		printerr(2, "%u sBIT gray bits not valid for %ubit/sample image\n",
			 sig_bit->gray, maxbits);
	    } else if (sig_bit->alpha == 0 || sig_bit->alpha > maxbits) {
		printerr(2, "%u sBIT alpha bits(tm) not valid for %ubit/sample image\n",
			 sig_bit->alpha, maxbits);
	    } else {
		fprintf(fpout, "    gray: %u; alpha: %u\n", sig_bit->gray, sig_bit->alpha);
	    }
	    break;
	case PNG_COLOR_TYPE_RGB_ALPHA:
	    if (sig_bit->gray == 0 || sig_bit->gray > maxbits) {
		printerr(1, "%u sBIT red bits not valid for %ubit/sample image",
			 sig_bit->gray, maxbits);
	    } else if (sig_bit->green == 0 || sig_bit->green > maxbits) {
		printerr(1, "%u sBIT green bits not valid for %ubit/sample image",
			 sig_bit->green, maxbits);
	    } else if (sig_bit->blue == 0 || sig_bit->blue > maxbits) {
		printerr(1, "%u sBIT blue bits not valid for %ubit/sample image",
			 sig_bit->blue, maxbits);
	    } else if (sig_bit->alpha == 0 || sig_bit->alpha > maxbits) {
		printerr(1, "%u sBIT alpha bits not valid for %ubit/sample image",
			 sig_bit->alpha, maxbits);
	    } else {
		fprintf(fpout, "    red: %u; green: %u; blue: %u; alpha: %u;\n",
			sig_bit->red, 
			sig_bit->green,
			sig_bit->blue,
			sig_bit->alpha);
	    }
	    break;
	}
	fprintf(fpout, "}\n");
    }
}

static void dump_pCAL(FILE *fpout)
{
    static char *mapping_type[] = {
	"linear", "euler", "exponential", "hyperbolic"
    };
    
    png_charp purpose;
    png_int_32 X0;
    png_int_32 X1;
    int type;
    int nparams;
    png_charp units;
    png_charpp params;

    if (png_get_pCAL(png_ptr, info_ptr, &purpose, &X0, &X1, &type, &nparams, &units, &params)) {

	if (type >= PNG_EQUATION_LAST)
	    printerr(1, "invalid equation type in pCAL");
	else {
	    int	i;

	    fprintf(fpout, "pCAL {\n");
	    fprintf(fpout, "    name: \"%s\";\n", safeprint(purpose));
	    fprintf(fpout, "    x0: %d;\n", X0);
	    fprintf(fpout, "    x1: %d;\n", X1);
	    fprintf(fpout, "    mapping: %s;        # equation type %u\n", 
		   mapping_type[type], type);
	    fprintf(fpout, "    unit: \"%s\"\n", safeprint(units));
	    if (nparams) {
		fprintf(fpout, "    parameters:");
		for (i = 0; i < nparams; i++)
		    fprintf(fpout, " %s", safeprint(params[i]));
		fprintf(fpout, ";\n");
	    }
	    fprintf(fpout, "}\n");
	}
    }
}

static void dump_sCAL(FILE *fpout)
{
#ifdef PNG_FLOATING_POINT_SUPPORTED
    int unit;
    double width;
    double height;

    if (png_get_sCAL(png_ptr, info_ptr, &unit, &width, &height)) {
#else
#ifdef PNG_FIXED_POINT_SUPPORTED
    int unit;
    png_charp swidth;
    png_charp sheight;

    if (png_get_sCAL_s(png_ptr, info_ptr, &unit, &swidth, &sheight)) {
#endif
#endif
	fprintf(fpout, "sCAL {\n");
	switch (unit)
	{
	case PNG_SCALE_METER:
	    fprintf(fpout, "    unit:   meter\n");
	    break;
	case PNG_SCALE_RADIAN:
	    fprintf(fpout, "    unit:   radian\n");
	    break;
	default:
	    fprintf(fpout, "    unit:   unknown\n");
	    break;
	}
#ifdef PNG_FLOATING_POINT_SUPPORTED
	fprintf(fpout, "    width:  %g\n", width);
	fprintf(fpout, "    height: %g\n", height);
#else
#ifdef PNG_FIXED_POINT_SUPPORTED
	fprintf(fpout, "    width:  %s\n", swidth);
	fprintf(fpout, "    height: %s\n", sheight);
#endif
#endif
	fprintf(fpout, "}\n");
    }
}

static void dump_sPLT(FILE *fpout)
{
/*
    for (i = 0; i < info_ptr->splt_palettes_num; i++)
	dump_sPLT(info_ptr->splt_palettes + i, fpout);
*/
    int i, j;
    int num_spalettes;
    png_sPLT_tp entries;

    num_spalettes = png_get_sPLT(png_ptr, info_ptr, &entries);
    
    for (j = 0; j < num_spalettes; j++)
    {
	png_sPLT_tp ep = entries + j;

	initialize_hash(hash_by_rgb, rgb_hashbuckets, &rgb_initialized);

	fprintf(fpout, "sPLT {\n");
	fprintf(fpout, "    name: \"%s\";\n", safeprint(ep->name));
	fprintf(fpout, "    depth: %u;\n", ep->depth);

	for (i = 0;  i < ep->nentries;  i++)
	{
	    char *name = 0;

	    fprintf(fpout, "    (%3u,%3u,%3u), %3u, %3u "
		    "    # rgba = [0x%02x,0x%02x,0x%02x,0x%02x]",
		    ep->entries[i].red,
		    ep->entries[i].green,
		    ep->entries[i].blue,
		    ep->entries[i].alpha,
		    ep->entries[i].frequency,
		    ep->entries[i].red,
		    ep->entries[i].green,
		    ep->entries[i].blue,
		    ep->entries[i].alpha);

	    if (rgb_initialized)
		name = find_by_rgb(ep->entries[i].red,
				   ep->entries[i].green,
				   ep->entries[i].blue);
	    if (name)
		fprintf(fpout, ", name = %s", name);

	    fprintf(fpout, ", freq = %u\n", ep->entries[i].frequency);
	}

	fprintf(fpout, "}\n");

    }
}

static void dump_tRNS(FILE *fpout)
{
    png_byte color_type = png_get_color_type(png_ptr, info_ptr);
    png_bytep trans;
    int num_trans;
    png_color_16p trans_values;

    if (png_get_tRNS(png_ptr, info_ptr, &trans, &num_trans, &trans_values)) {
	int	i;

	fprintf(fpout, "tRNS {\n");
	switch (color_type) {
	case PNG_COLOR_TYPE_GRAY:
	    fprintf(fpout, "    gray: %u;\n", trans_values->gray);
	    break;
	case PNG_COLOR_TYPE_RGB:
	    fprintf(fpout, "    red: %u; green: %u; blue: %u;\n",
		    trans_values->red,
		    trans_values->green,
		    trans_values->blue);
	    break;
	case PNG_COLOR_TYPE_PALETTE:
	    for (i = 0; i < num_trans; i++)
		fprintf(fpout, " %u", trans[i]);
	    break;
	case PNG_COLOR_TYPE_GRAY_ALPHA:
	case PNG_COLOR_TYPE_RGB_ALPHA:
	    printerr(1, "tRNS chunk illegal with this image type");
	    break;
	}
	fprintf(fpout, "}\n");
    }
}

static void dump_sRGB(FILE *fpout)
{
    int intent;

    if (png_get_sRGB(png_ptr, info_ptr, &intent)) {
	if (intent < 0 || intent > 3) {
	    printerr(1, "sRGB invalid rendering intent %d", intent);
	} else {
	    fprintf(fpout, "sRGB {%u;}             # %s\n",
		   intent,
		   rendering_intent[intent]);
	}
    }
}

static void dump_tIME(FILE *fpout)
{
    png_timep mod_time;

    if (png_get_tIME(png_ptr, info_ptr, &mod_time)) {
	static char *months[] =
	{"(undefined)", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
	 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
	char *month;

	if (mod_time->month < 1 || mod_time->month > 12)
	    month = months[0];
	else
	    month = months[mod_time->month];

	fprintf(fpout, "tIME {\n");
	fprintf(fpout, "    # %2d %s %4d %02d:%02d:%02d GMT\n", 
		mod_time->day,
		month,
		mod_time->year,
		mod_time->hour,
		mod_time->minute,
		mod_time->second);
	fprintf(fpout, "    year:   %u\n", mod_time->year);
	fprintf(fpout, "    month:  %u\n", mod_time->month);
	fprintf(fpout, "    day:    %u\n", mod_time->day);
	fprintf(fpout, "    hour:   %u\n", mod_time->hour);
	fprintf(fpout, "    minute: %u\n", mod_time->minute);
	fprintf(fpout, "    second: %u\n", mod_time->second);
	fprintf(fpout, "}\n");
    }
}

static void dump_text(FILE *fpout)
{
    png_textp text_ptr;
    int num_text;

    if (png_get_text(png_ptr, info_ptr, &text_ptr, &num_text)) {
	int	i;

	for (i = 0; i < num_text; i++)
	{
	    switch (text_ptr[i].compression)
	    {
	    case PNG_TEXT_COMPRESSION_NONE:
		fprintf(fpout, "tEXt {\n");
		fprintf(fpout, "    keyword: \"%s\";\n", 
			safeprint(text_ptr[i].key));
		break;

	    case PNG_TEXT_COMPRESSION_zTXt:
		fprintf(fpout, "zTXt {\n");
		fprintf(fpout, "    keyword: \"%s\";\n", 
			safeprint(text_ptr[i].key));
		break;

	    case PNG_ITXT_COMPRESSION_NONE:
	    case PNG_ITXT_COMPRESSION_zTXt:
		fprintf(fpout, "iTXt {\n");
#ifdef PNG_iTXt_SUPPORTED
		fprintf(fpout, "    language: \"%s\";\n", 
			safeprint(text_ptr[i].lang));
#endif /* PNG_iTXt_SUPPORTED */
		fprintf(fpout, "    keyword: \"%s\";\n", 
			safeprint(text_ptr[i].key));
#ifdef PNG_iTXt_SUPPORTED
		fprintf(fpout, "    translated: \"%s\";\n", 
			safeprint(text_ptr[i].lang_key));
#endif /* PNG_iTXt_SUPPORTED */
		break;
	    }

	    fprintf(fpout, "    text: \"%s\";\n", 
		    safeprint(text_ptr[i].text));
	    fprintf(fpout, "}\n");
	}
    }
}

static void dump_unknown_chunks(int after_idat, FILE *fpout)
{
    png_unknown_chunkp entries;
    int num_unknown_chunks;
    int	i;

    num_unknown_chunks = png_get_unknown_chunks(png_ptr, info_ptr, &entries);

    for (i = 0; i < num_unknown_chunks; i++)
    {
	png_unknown_chunk	*up = entries + i;

	/* are we before or after the IDAT part? */
	if (after_idat != !!(up->location & PNG_AFTER_IDAT))
	    continue;

/* macros to extract big-endian short and long ints */
#define SH(p) ((unsigned short)((p)[1]) | (((p)[0]) << 8))
#define LG(p) ((unsigned long)(SH((p)+2)) | ((ulg)(SH(p)) << 16))

	if (!memcmp(up->name, "gIFg", 5))
	{
	  fprintf(fpout, "gIFg {\n");
	    fprintf(fpout, "    disposal: %d; input: %d; delay %f;\n",
		    up->data[0], up->data[1], (float)(.01 * SH(up->data+2)));
	}
	else if (!memcmp(up->name, "gIFx", 5))
	{
	    fprintf(fpout, "gIFx {\n");
	    fprintf(fpout, "    identifier: \"%.*s\"; code: \"%c%c%c\"\n",
		    8, up->data, up->data[8], up->data[9], up->data[10]);
	    dump_data(fpout, "    data: ", up->size - 11, up->data + 11);
	}
	else
	{
	    fprintf(fpout, "private %s {\n", up->name);
	    dump_data(fpout, "    ", up->size, up->data);
	}
	fprintf(fpout, "}\n");
#undef SH
#undef LG
    }
}

/*****************************************************************************
 *
 * Compiler main sequence
 *
 *****************************************************************************/

void sngdump(png_byte *row_pointers[], FILE *fpout)
/* dump a canonicalized SNG form of a PNG file */
{
    fprintf(fpout, "#SNG: from %s\n", current_file);

    dump_IHDR(fpout);			/* first critical chunk */

    dump_cHRM(fpout);
    dump_gAMA(fpout);
    dump_iCCP(fpout);
    dump_sBIT(fpout);
    dump_sRGB(fpout);

    dump_PLTE(fpout);			/* second critical chunk */

    dump_bKGD(fpout);
    dump_hIST(fpout);
    dump_tRNS(fpout);
    dump_pHYs(fpout);
    dump_sPLT(fpout);
    dump_oFFs(fpout);
    dump_pCAL(fpout);
    dump_sCAL(fpout);

    dump_unknown_chunks(FALSE, fpout);

    /*
     * This is the earliest point at which we could write the image data;
     * the ancillary chunks after this point have no order contraints.
     * We choose to write the image last so that viewers/editors can get
     * a look at all the ancillary information.
     */

    dump_tIME(fpout);
    dump_text(fpout);

    dump_image(row_pointers, fpout);	/* third critical chunk */

    dump_unknown_chunks(TRUE, fpout);
}

int sngd(FILE *fp, char *name, FILE *fpout)
/* read and decompile an SNG image presented on stdin */
{
#ifndef PNG_INFO_IMAGE_SUPPORTED
    png_bytepp row_pointers;
    png_uint_32 row;
    png_uint_32 height;
#endif

   current_file = name;
   sng_error = 0;

   /* Create and initialize the png_struct with the desired error handler
    * functions.  If you want to use the default stderr and longjump method,
    * you can supply NULL for the last three parameters.  We also supply the
    * the compiler header file version, so that we know if the application
    * was compiled with a compatible version of the library.  REQUIRED
    */
   png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

   if (png_ptr == NULL)
   {
      fclose(fp);
      return(1);
   }

   /* Allocate/initialize the memory for image information.  REQUIRED. */
   info_ptr = png_create_info_struct(png_ptr);
   if (info_ptr == NULL)
   {
      fclose(fp);
      png_destroy_read_struct(&png_ptr, (png_infopp)NULL, (png_infopp)NULL);
      return 1;
   }

   /* Set error handling if you are using the setjmp/longjmp method (this is
    * the normal method of doing things with libpng).  REQUIRED unless you
    * set up your own error handlers in the png_create_read_struct() earlier.
    */
   if (setjmp(png_jmpbuf(png_ptr)))
   {
      /* Free all of the memory associated with the png_ptr and info_ptr */
      png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
      fclose(fp);
      /* If we get here, we had a problem reading the file */
      return(1);
   }

   /* keep all unknown chunks, we'll dump them later */
   png_set_keep_unknown_chunks(png_ptr, 2, NULL, 0);

   /* treat IDAT as unknown so it gets passed through raw */
   if (idat)
   {
       png_set_keep_unknown_chunks(png_ptr, 2, (png_byte *)"IDAT", 1);
   }


   /* Set up the input control if you are using standard C streams */
   png_init_io(png_ptr, fp);


   /*
    * Unpack images with bit depth < 8 into bytes per sample.
    * We'll cheat, later on, by referring to png_ptr->bit_depth.
    * This preserves the bit depth of the file, as opposed to
    * the unpacked bit depth of the image.  But this is
    * undocumented.  If it ever breaks, the regression test
    * will start failing on images of depth 1, 2, and 4.
    */
#ifdef PNG_INFO_IMAGE_SUPPORTED
   png_read_png(png_ptr, info_ptr, PNG_TRANSFORM_PACKING, NULL);

   /* dump the image */
   sngdump(png_get_rows(png_ptr, info_ptr), fpout);
#else
   png_set_packing(png_ptr);

   /* The call to png_read_info() gives us all of the information from the
    * PNG file before the first IDAT (image data chunk).  REQUIRED
    */
   png_read_info(png_ptr, info_ptr);

   png_read_update_info(png_ptr, info_ptr);

   height = png_get_image_height(png_ptr, info_ptr);

   row_pointers = (png_bytepp)malloc(height * sizeof(png_bytep));
   for (row = 0; row < height; row++)
       row_pointers[row] = malloc(png_get_rowbytes(png_ptr, info_ptr));

   png_read_image(png_ptr, row_pointers);

   /* read rest of file, and get additional chunks in info_ptr - REQUIRED */
   png_read_end(png_ptr, info_ptr);

   /* dump the image */
   sngdump(row_pointers, fpout);
#endif

   /* clean up after the read, and free any memory allocated - REQUIRED */
   png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);

   /* close the file */
   fclose(fp);

   /* that's it; return this file's error status */
   return sng_error;
}

/* sngd.c ends here */
