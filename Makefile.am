# Makefile - top level

# files to include in the distro but not build
EXTRA_DIST = \
		$(top_srcdir)/template \
		$(top_srcdir)/template-old \
		$(top_srcdir)/test \
		$(top_srcdir)/debian \
		$(top_srcdir)/radlib-api-reference.html \
		$(top_srcdir)/MAINTAIN \
		$(top_srcdir)/config-radlib-arm-linux


# this target removes CVS directories
dist-hook:
	rm -rf `find $(distdir) -name .svn`
	chmod ugo+x $(distdir)/install-sh


# add subdirectories to be included in the distribution 
# (thus requiring Makefile.am files)
SUBDIRS = src msgRouter debug
