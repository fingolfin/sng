## Process this file with automake to produce Makefile.in
bin_PROGRAMS = sng
bin_SCRIPTS = sng_regress
sng_SOURCES = main.c sngc.c sngd.c sng.h
sng_LDFLAGS = -static
man_MANS = sng.1
# The man pages and script are here because automake has a bug
EXTRA_DIST = Makefile sng.spec.in sng.spec sng.1 sng_regress indexgen.sh lsmgen.sh

RPMROOT=/usr/src/redhat
rpm: dist
	cp @PACKAGE@-@VERSION@.tar.gz $(RPMROOT)/SOURCES;
	cp @PACKAGE@.spec $(RPMROOT)/SPECS
	cd $(RPMROOT)/SPECS; rpm -ba @PACKAGE@.spec	
	cp $(RPMROOT)/RPMS/`arch|sed 's/i[4-9]86/i386/'`/@PACKAGE@-@VERSION@*.rpm .
	cp $(RPMROOT)/SRPMS/@PACKAGE@-@VERSION@*.src.rpm .

zip: $(DISTFILES)
	zip @PACKAGE@-@VERSION@.zip $(DISTFILES)

# Regression-test sng.  Passes if no differences show up.
regress:
	sng_regress test.sng
