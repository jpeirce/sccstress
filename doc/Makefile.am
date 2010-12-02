info_TEXINFOS = sccstress.texi
man_MANS = sccstress.1
EXTRA_DIST = $(man_MANS) sccstress.html

MAINTAINERCLEANFILES = Makefile.in mdate-sh texinfo.tex sccstress.html $(man_MANS)

sccstress.1:
	help2man -n 'tool to impose load on and sccstress test systems' ../src/sccstress > $@

sccstress.html: sccstress.texi
	$(MAKEINFO) --no-split --html $<
