ifeq ($(OS), Windows_NT)
	PIC_FLAG =
	STATIC_EXT = lib
	DYNAMIC_EXT = dll
else
	PIC_FLAG = -fPIC
	STATIC_EXT = a
	DYNAMIC_EXT = so
endif

STATIC_LIBS = libsref.$(STATIC_EXT)
SHARED_LIBS = libsref.$(DYNAMIC_EXT)

HEADERS = sref.h

OBJS = sref.o
LOBJS = $(OBJS:.o=.lo)

TEST_OBJS = $(LOBJS)

ALL_LIBS = $(STATIC_LIBS) $(SHARED_LIBS)

-include config.mak

AR = $(CROSS_COMPILE)ar
RANLIB = $(CROSS_COMPILE)ranlib
CFLAGS += $(CFLAGS_AUTO) -D_DEFAULT_SOURCE

all: $(ALL_LIBS)

check: $(TEST_OBJS)
	$(CC) $(CFLAGS) tests/test.c $(TEST_OBJS) -o tst
	./tst

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

%.lo: %.c $(HEADERS)
	$(CC) $(CFLAGS) $(PIC_FLAG) -c $< -o $@

libsref.$(STATIC_EXT): $(OBJS)
	rm -f $@
	$(AR) rc $@ $(OBJS)
	$(RANLIB) $@

libsref.$(DYNAMIC_EXT): $(LOBJS)
	$(CC) -fPIC -shared $(CFLAGS) -o $@ $(LOBJS)

install: $(ALL_LIBS)
	mkdir -p $(includedir)/sref
	cp libsref* $(libdir)/
	cp $(HEADERS) $(includedir)/sref

clean:
	rm -rf *.o *.lo libsref.* tst

