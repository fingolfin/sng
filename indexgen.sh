#!/bin/sh

version=`sed -n -e "/AM_INIT_AUTOMAKE(sng, \(.*\))/s//\\1/p" <configure.in`
date=`date "+%d %b %Y"`

cat >index.html <<EOF
<!doctype HTML public "-//W3O//DTD W3 HTML 3.2//EN">
<HTML>
<HEAD>
<link rev=made href=mailto:esr@snark.thyrsus.com>
<meta name="description" content="Home page of the sng compiler/decompiler">
<meta name="keywords" content="SNG, PNG, converter, graphics"> 
<TITLE>The sng home page</TITLE>
</HEAD>
<BODY>
<table width="100%" cellpadding=0><tr>
<td width="30%">Back to <a href="http://www.tuxedo.org/~esr">Eric's Home Page</a>
<td width="30%" align=center>Up to <a href="http://www.tuxedo.org/~esr/sitemap.html">Site Map</a>
<td width="30%" align=right>$date
</table>
<HR>

<H1 ALIGN=CENTER>The SNG home page</H1>

SNG (Scriptable Network Graphics) is a minilanguage designed
specifically to represent the entire contents of a PNG (Portable
Network Graphics) file in an editable form.  Thus, SNGs representing
elaborate graphics images and ancillary chunk data can be readily
generated or modified using only text tools.<P>

SNG is implemented by a compiler/decompiler called sng that losslessly
translates between SNG and PNG.<P>

The current version is ${version}. The program is now in beta release.
It is quite functional, and has been tested on all 155 of the non-broken
images in the png test suite, but the design of SNG is not yet frozen.
The syntax of the language may change (possibly radically) before a
production release.<p>

You can download a <a
href="sng-${version}.tar.gz">source
tarball</a>, or a <a
href="sng-${version}.zip">source ZIP
archive</a>.  RPMs are not yet available, but will be after the next
major libpng release.<P>

Note: To use sng, you will need to install libpng-1.1.0f or later.
You can download it <a
href="ftp://swrinde.nde.swri.edu/pub/png-group/src">here</a>).<p>

(I am a credited contributor to the libpng reference library, and also
maintain <A HREF="http://www.tuxedo.org/~esr/gif2png/">gif2png</A>.
For more information on the PNG format and associated tools, see the
official <A HREF="http://www.libpng.org/pub/png/">PNG website</A>)<P>

<HR>
<table width="100%" cellpadding=0><tr>
<td width="30%">Back to <a href="http://www.tuxedo.org/~esr">Eric's Home Page</a>
<td width="30%" align=center>Up to <a href="http://www.tuxedo.org/~esr/sitemap.html">Site Map</a>
<td width="30%" align=right>$date
</table>

<P><ADDRESS>Eric S. Raymond <A HREF="mailto:esr@thyrsus.com">&lt;esr@thyrsus.com&gt;</A></ADDRESS>
</BODY>
</HTML>
EOF
