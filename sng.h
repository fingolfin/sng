/* sng.h -- interface to the SNG compiler */

extern int sngc(FILE *fin, char *file, FILE *fout);
extern int sngd(FILE *fin, char *file, FILE *fout);

extern int verbose;
extern int idat;
extern int sng_error;

#define SUCCEED	0
#define FAIL	-1

#define TRUE	1
#define FALSE	0

/* RFC2045 encoding */
#define BASE64	"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"

/*
 * Maximum string size -- the size of an IDAT buffer minus the minimum overhead
 * of a string chunk (that is, the overhead of a minimal tEXt chunk).  
 * That overhead: four characters of chunk name, plus zero characters of 
 * keyword, plus one character of NUL separator.
 */
#define PNG_STRING_MAX_LENGTH	(PNG_ZBUF_SIZE - 5)

/* sng.h ends here */
