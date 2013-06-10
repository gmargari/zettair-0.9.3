## run automake over this file to create Makefile.in (which then needs
## to be processed by an autoconf configure script to create Makefile,
## but don't get me started on this whole process :o( )

## FIXME: automate removal of update stuff

# necessary compiler flags (not that cflags is not supposed to be set
# for optimisation, as the user is supposed to do that)
AM_CPPFLAGS = -I $(srcdir)/src/include/ -I $(srcdir)/include/ \
              -I $(srcdir)/src/include/compat/ \
              -I $(srcdir)/src/include/@OSINCLUDE@/ \
              -D@OS@ -DLOGERRORS
AM_CFLAGS = 
AM_LDFLAGS = @LIBS@

# declaration of what to build (hint: nodist only seems to apply to SOURCES)
bin_PROGRAMS = zet zet_trec zet_cat zet_impactify
data_DATA = config/psettings.xml
lib_LTLIBRARIES = libzet.la
noinst_PROGRAMS = zet_diff zet_file zet_dict \
                  src/test/str_1 \
                  src/test/bucket_1 src/test/mem_1 \
                  src/test/iobtree_1 src/test/heap_1 \
                  src/test/getlongopt_1 src/test/binsearch_1 \
                  src/test/bit_1 src/test/stack_1 src/test/rbtree_1 \
                  src/test/mlparse_1 src/test/freemap_1 \
                  src/test/docmap_1 \
                  src/test/stop_1 src/test/poolalloc_1 \
                  \
                  mlparse.test mlparse_wrap.test mime.test poolalloc.test \
                  psettings_gen psettings_gen.test stop_gen staticalloc.test \
                  objalloc.test lcrand.test stem.test btree chash.test \
				  hashtime

noinst_HEADERS = src/include/heap.h       include/index.h \
                 src/include/index_querybuild.h \
                 src/include/makeindex.h  src/include/uri.h \
                 src/include/_index.h     src/include/merge.h \
                 src/include/_postings.h  src/include/mlparse.h \
                 src/include/_uri.h       src/include/postings.h \
                 src/include/binsearch.h  src/include/stop.h \
                 src/include/bit.h        src/include/str.h \
                 src/include/chash.h      src/include/_chash.h \
                 src/include/config_h.in  \
                 src/include/def.h        src/include/timings.h \
                 src/include/freemap.h    src/include/getlongopt.h \
                 src/include/vec.h        src/include/poolalloc.h \
                 src/include/bucket.h     src/include/test.h \
                 src/include/fdset.h      src/include/queryparse.h \
                 src/include/pyramid.h    src/include/iobtree.h \
                 src/include/getmaxfsize.h \
                 src/include/storagep.h   \
                 src/include/btbucket.h   src/include/_btbucket.h \
                 src/include/mem.h        src/include/_mem.h \
                 src/include/vocab.h      src/include/ascii.h \
                 src/include/mlparse_wrap.h src/include/btbulk.h \
                 src/include/error.h      src/include/mime.h \
                 src/include/summarise.h  src/include/stem.h \
                 src/include/stack.h      src/include/rbtree.h \
                 src/include/signals.h    src/include/alloc.h \
				 src/include/staticalloc.h \
				 src/include/metric.h     src/include/search.h \
                 src/include/psettings.h  src/include/lcrand.h \
                 src/include/objalloc.h   src/include/stream.h \
                 src/include/crc.h        src/include/trec_eval.h \
                 src/include/docmap.h     src/include/_docmap.h \
                 src/include/reposset.h   src/include/_reposset.h \
				 src/include/impact.h     src/include/impact_build.h \
                 src/include/testutils.h \
                 src/include/compat/zstdint.h \
                 src/include/compat/zvalgrind.h \
                 src/include/linux/firstinclude.h \
                 src/include/freebsd/firstinclude.h \
                 src/include/sunos/firstinclude.h \
                 src/include/cygwin/firstinclude.h \
                 src/include/darwin/firstinclude.h \
                 src/include/win32/firstinclude.h \
                 src/include/win32/config.h src/include/compat/zglob.h \
                 src/include/svnversion.h

EXTRA_DIST = config/psettings.xml config/stoplist_sorted \
             txt/moby.txt txt/README \
             src/compat/win32_stubs.c \
             src/compat/zglob.c doc/Build.html doc/Content.html \
             src/pyzet/zetmodule.c src/pyzet/pzet.py \
             doc/copying.html doc/Credits.html doc/Disclaimer.html \
             doc/hacking.html doc/index.html doc/logo.gif \
             doc/Readme.html doc/README.TXT doc/Search.html \
             doc/style.css doc/Tutorial.html \
             scripts/metric.py scripts/mime.py src/metric.c \
             src/dirichlet.metric src/pcosine.metric \
             src/cosine.metric src/hawkapi.metric src/okapi_k3.metric \
             \
             win32/visualc6/zettair.dsw \
             win32/visualc6/zet.dsp \
             win32/visualc6/libzet.dsp \
             win32/visualc6/zet_cat.dsp \
             win32/visualc6/zet_trec.dsp \
             win32/visualc6/zet_diff.dsp \
             \
             src/test/binsearch_1.cases \
             src/test/bucket_1.cases \
             src/test/bufstuffer_1.cases \
             src/test/docmap_1.cases \
             src/test/freemap_1.cases \
             src/test/getlongopt_1.cases \
             src/test/heap_1.cases \
             src/test/iobtree_1.cases \
             src/test/rbtree_1.cases \
             src/test/stack_1.cases \
             src/test/stop_1.cases

# dependencies for the main programs/libraries

zet_SOURCES = src/commandline.c src/include/svnversion.h
zet_LDADD = -lzet -L.
zet_DEPENDENCIES = libzet.la

zet_trec_SOURCES = src/trecrun.c src/trec_eval.c
zet_trec_LDADD = -lzet -L.
zet_trec_DEPENDENCIES = libzet.la

library_sources = src/str.c src/index.c src/mlparse.c \
                  src/stop.c src/stop_default.c \
                  src/postings.c \
                  src/merge.c src/vec.c src/makeindex.c \
                  src/freemap.c src/bit.c src/binsearch.c \
                  src/search.c src/chash.c src/stem.c \
                  src/heap.c src/queryparse.c src/index_querybuild.c \
                  src/bucket.c src/mem.c src/fdset.c \
                  src/pyramid.c src/iobtree.c src/getmaxfsize.c \
                  src/storagep.c src/btbucket.c src/btbulk.c \
                  src/vocab.c src/getlongopt.c src/error.c \
                  src/mlparse_wrap.c src/summarise.c src/mime.c \
                  src/remerge.c src/signals.c \
                  src/stack.c src/rbtree.c \
                  src/psettings.c src/psettings_default.c src/lcrand.c \
                  src/objalloc.c \
                  src/docmap.c src/reposset.c \
                  src/poolalloc.c src/alloc.c src/staticalloc.c \
				  src/dirichlet.c src/pcosine.c \
				  src/cosine.c src/hawkapi.c src/okapi_k3.c \
				  src/impact.c src/impact_build.c \
                  \
                  src/libtextcodec/crc.c src/libtextcodec/stream.c \
                  src/libtextcodec/detectfilter.c \
                  src/libtextcodec/gunzipfilter.c

libzet_la_SOURCES = $(library_sources)

# depedencies for utility programs

zet_cat_SOURCES = tools/cat.c 
zet_cat_LDADD = -lzet -L. 
zet_cat_DEPENDENCIES = libzet.la

zet_dict_SOURCES = tools/dict.c
zet_dict_LDADD = -lzet -L. 
zet_dict_DEPENDENCIES = libzet.la

zet_file_SOURCES = tools/file.c src/mime.c src/str.c
zet_file_CFLAGS = -DHACK

zet_diff_SOURCES = tools/diff.c
zet_diff_LDADD = -lzet -L. 
zet_diff_DEPENDENCIES = libzet.la

zet_impactify_SOURCES = tools/impactify.c
zet_impactify_LDADD = -lzet -L. 
zet_impactify_DEPENDENCIES = libzet.la

btree_SOURCES = tools/btree.c
btree_LDADD = -lzet -L. 
btree_DEPENDENCIES = libzet.la

hashtime_SOURCES = tools/hashtime.c src/chash.c src/str.c src/alloc.c \
                   src/objalloc.c src/mem.c src/bit.c
hashtime_CFLAGS = -DHACK

# misc stuff

stop_gen_SOURCES = src/stop.c src/str.c src/poolalloc.c src/chash.c \
	               src/alloc.c src/bit.c src/objalloc.c src/mem.c
stop_gen_CFLAGS = -DSTOP_MAIN

psettings_gen_SOURCES = src/psettings.c src/chash.c src/str.c \
                        src/mlparse_wrap.c src/mlparse.c src/bit.c \
                        src/mime.c src/objalloc.c src/mem.c \
						src/alloc.c src/error.c
psettings_gen_CFLAGS = -DPSETTINGS_MAIN

psettings_gen_test_SOURCES = src/psettings.c src/chash.c src/str.c \
                        src/mlparse_wrap.c src/mlparse.c src/bit.c \
                        src/mime.c src/psettings_default.c src/objalloc.c \
						src/mem.c src/alloc.c src/error.c
psettings_gen_test_CFLAGS = -DPSETTINGS_DEFAULT_TEST

poolalloc_test_SOURCES = src/poolalloc.c src/chash.c src/mem.c src/bit.c \
                         src/objalloc.c src/alloc.c src/str.c
poolalloc_test_CFLAGS = -DPOOLALLOC_TEST

staticalloc_test_SOURCES = src/staticalloc.c src/mem.c src/bit.c
staticalloc_test_CFLAGS = -DSTATICALLOC_TEST

mlparse_test_SOURCES = src/mlparse.c src/str.c src/libtextcodec/stream.c \
                       src/libtextcodec/gunzipfilter.c \
                       src/libtextcodec/detectfilter.c \
                       src/libtextcodec/crc.c
mlparse_test_CFLAGS = -DMLPARSE_TEST

mime_test_SOURCES = src/mime.c src/str.c
mime_test_CFLAGS = -DMIME_TEST

mlparse_wrap_test_SOURCES = src/mlparse_wrap.c src/mlparse.c src/str.c
mlparse_wrap_test_CFLAGS = -DMLPARSE_WRAP_TEST

objalloc_test_SOURCES = src/objalloc.c src/bit.c src/mem.c src/str.c \
                        src/alloc.c
objalloc_test_CFLAGS = -DOBJALLOC_TEST 

lcrand_test_SOURCES = src/lcrand.c 
lcrand_test_CFLAGS = -DLCRAND_TEST 

chash_test_SOURCES = src/chash.c src/objalloc.c src/mem.c \
                     src/bit.c src/str.c src/alloc.c
chash_test_CFLAGS = -DCHASH_TEST 

stem_test_SOURCES = src/stem.c src/chash.c src/objalloc.c src/mem.c \
                    src/bit.c src/str.c src/alloc.c
stem_test_CFLAGS = -DSTEM_TEST 

# more formal testing stuff

# list of tests to run
TESTS = src/test/binsearch_1 src/test/iobtree_1 src/test/stack_1 \
        src/test/bit_1 src/test/mem_1 src/test/stop_1 \
        src/test/bucket_1 src/test/mlparse_1 src/test/str_1 \
        \
        src/test/freemap_1 src/test/docmap_1 \
        src/test/getlongopt_1 src/test/poolalloc_1 src/test/heap_1 \
		src/test/rbtree_1

# dependencies for tests

# -DHACK added to CFLAGS so that libtool won't complain about having to
# compile them seperately :o(

#src_test_test_1_SOURCES = src/test/test_1.c
#src_test_test_1_LDADD = -lzet -L. 
#src_test_test_1_DEPENDENCIES = libzet.la

src_test_str_1_SOURCES = src/test/str_1.c src/test.c src/str.c \
                         src/getlongopt.c
src_test_str_1_CFLAGS = -DHACK

src_test_heap_1_SOURCES = src/test/heap_1.c src/test.c src/str.c \
                          src/heap.c src/bit.c src/getlongopt.c src/lcrand.c
src_test_heap_1_CFLAGS = -DHACK

src_test_bucket_1_SOURCES = src/test/bucket_1.c src/test.c \
                            src/bucket.c src/str.c \
                            src/vec.c src/chash.c src/objalloc.c \
                            src/bit.c src/getlongopt.c src/mem.c \
							src/alloc.c
src_test_bucket_1_CFLAGS = -DHACK

src_test_mem_1_SOURCES = src/test/mem_1.c src/test.c \
                         src/mem.c src/getlongopt.c src/str.c src/bit.c
src_test_mem_1_CFLAGS = -DHACK

src_test_iobtree_1_SOURCES = src/test/iobtree_1.c src/test.c \
                             src/bucket.c src/str.c src/vec.c \
                             src/bit.c src/iobtree.c src/binsearch.c \
                             src/fdset.c src/chash.c src/btbucket.c \
                             src/freemap.c src/getlongopt.c src/rbtree.c \
                             src/getmaxfsize.c src/mem.c src/stack.c \
                             src/lcrand.c src/objalloc.c src/alloc.c
src_test_iobtree_1_CFLAGS = -DHACK

src_test_binsearch_1_SOURCES = src/test/binsearch_1.c src/test.c \
                               src/binsearch.c src/getlongopt.c src/str.c \
                               src/lcrand.c
src_test_binsearch_1_CFLAGS = -DHACK

src_test_bit_1_SOURCES = src/test/bit_1.c src/test.c \
                         src/bit.c src/getlongopt.c src/str.c
src_test_bit_1_CFLAGS = -DHACK

src_test_getlongopt_1_SOURCES = src/test.c src/test/getlongopt_1.c \
                                src/str.c src/getlongopt.c
src_test_getlongopt_1_CFLAGS = -DHACK

src_test_stack_1_SOURCES = src/test.c src/test/stack_1.c \
                           src/str.c src/getlongopt.c src/stack.c
src_test_stack_1_CFLAGS = -DHACK

src_test_rbtree_1_SOURCES = src/test.c src/test/rbtree_1.c \
                           src/str.c src/getlongopt.c src/stack.c \
                           src/rbtree.c src/bit.c src/lcrand.c src/objalloc.c \
                           src/mem.c src/alloc.c
src_test_rbtree_1_CFLAGS = -DHACK

src_test_mlparse_1_SOURCES = src/test.c src/test/mlparse_1.c \
                           src/str.c src/getlongopt.c src/mlparse.c
src_test_mlparse_1_CFLAGS = -DHACK

src_test_freemap_1_SOURCES = src/test.c src/test/freemap_1.c \
                             src/str.c src/getlongopt.c src/freemap.c \
                             src/binsearch.c src/rbtree.c src/stack.c \
                             src/bit.c src/lcrand.c src/objalloc.c \
                             src/mem.c src/alloc.c
src_test_freemap_1_CFLAGS = -DHACK

src_test_docmap_1_SOURCES = src/test.c src/test/docmap_1.c \
                            src/docmap.c src/reposset.c \
                            src/str.c src/getlongopt.c \
                            src/lcrand.c src/vec.c src/error.c \
                            src/freemap.c src/fdset.c src/bit.c \
                            src/chash.c src/rbtree.c src/objalloc.c \
                            src/stack.c src/mem.c src/testutils.c \
                            src/alloc.c src/binsearch.c src/mime.c
src_test_docmap_1_CFLAGS = -DHACK
src_test_docmap_1_LDADD = 

src_test_stop_1_SOURCES = src/test.c src/test/stop_1.c \
                           src/stop.c src/getlongopt.c src/error.c \
                           src/str.c src/poolalloc.c src/chash.c \
                           src/bit.c src/objalloc.c src/mem.c \
						   src/alloc.c
src_test_stop_1_CFLAGS = -DHACK

src_test_poolalloc_1_SOURCES = src/test.c src/test/poolalloc_1.c \
                           src/getlongopt.c src/error.c \
                           src/str.c src/poolalloc.c src/chash.c \
                           src/bit.c src/objalloc.c src/mem.c \
						   src/alloc.c
src_test_poolalloc_1_CFLAGS = -DHACK

BUILT_SOURCES = src/mime.c src/include/mime.h src/include/svnversion.h

#
# PyZET
#
# Not very portable, but should work
#
# FIXME automatic regeneration of setup.py from setup.py.in not working
if HAVE_PYTHON
pyzet:
	$(PYTHON) setup.py build

pyzet-install:
	$(PYTHON) setup.py install --prefix=${prefix}
	@echo "Set the env variable PYTHONPATH to ${prefix}/lib/python${PYTHON_VERSION}/site-packages for python to find the zet module"
	@echo "The directory holding libzet.so will also have to be in your LD_LIBRARY_PATH or similarly configured"
endif

#### THINGS AFTER THIS LINE WILL BE REMOVED PRIOR TO DISTRIBUTION ####
