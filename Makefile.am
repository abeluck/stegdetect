AUTOMAKE_OPTIONS = foreign no-dependencies

JPEGDIR		= ./jpeg-6b
JPEGINC		= -I$(JPEGDIR)
JPEGLIB		= -L$(JPEGDIR) -ljpeg
JPEGDEP		= $(JPEGDIR)/libjpeg.a

FILEDIR		= ./file
FILEINC		= -I$(FILEDIR)
FILELIB		= -L$(FILEDIR) -lfile
FILEDEP		= $(FILEDIR)/libfile.a

GTKINC		= @GTKINC@
GTKLIB		= @GTKLIB@
EVENTINC	= @EVENTINC@
EVENTLIB	= @EVENTLIB@

LIBS		= $(JPEGLIB)

SUBDIRS		= jpeg-6b file

man_MANS = stegdetect.1 stegbreak.1

EXTRA_DIST = $(man_MANS) acconfig.h jpeg-6b bf_locl.h bf_pi.h blowfish.h \
	compat/err.h compat/md5.h compat/sys/queue.h

#CFLAGS	= @CFLAGS@ -Wall -g
CFLAGS	= -O2 -Wall -g

INCLUDES = $(JPEGINC) $(FILEINC) -I$(srcdir)/compat $(EVENTINC) $(GTKINC)

EXTRA_PROGRAMS = xsteg
bin_PROGRAMS = stegdetect stegbreak stegcompare stegdeimage @XSTEG@

CSRCS=		common.c common.h jphide_table.c util.c jphide_table.h

stegdetect_SOURCES = $(CSRCS) stegdetect.c chi2cdf.c chi2cdf.h extraction.c \
	extraction.h discrimination.c discrimination.h math.c dct.c \
	dct.h jutil.c jutil.h f5.c
stegdetect_LDADD = @LIBOBJS@ $(LIBS) $(FILELIB) -lm

EXTRA_stegbreak_SOURCES = bf_enc.c bf-586.s
stegbreak_SOURCES = $(CSRCS) stegbreak.c \
		break_jphide.c break_jphide.h \
		break_outguess.c break_outguess.h \
		break_jsteg.c break_jsteg.h \
		cfg.c cfg.h rpp.c rpp.h \
		rules.c rules.h bf_skey.c db.c db.h \
		arc4.c arc4.h
stegbreak_LDADD = @LIBOBJS@ $(LIBS) $(FILELIB) @BFOBJ@
stegbreak_DEPENDENCIES = @BFOBJ@

stegcompare_SOURCES = $(CSRCS) stegcompare.c
stegcompare_LDADD = @LIBOBJS@ $(LIBS)

stegdeimage_SOURCES = $(CSRCS) stegdeimage.c
stegdeimage_LDADD = @LIBOBJS@ $(LIBS)

xsteg_SOURCES = xsteg.c xsteg.h xsteg_xpm.c
xsteg_LDADD = @LIBOBJS@ $(GTKLIB) $(EVENTLIB)

DISTCLEANFILES = *~

config.status:
