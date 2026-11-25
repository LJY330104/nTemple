DEBUG = FALSE

GCC = nspire-gcc
AS  = nspire-as
GXX = nspire-g++
LD  = nspire-ld
GENZEHN = genzehn

GCCFLAGS = -Wall -W -marm
LDFLAGS =
ZEHNFLAGS = --name "nTemple"

ifeq ($(DEBUG),FALSE)
	GCCFLAGS += -Os
else
	GCCFLAGS += -O0 -g
endif

OBJS = main.o game.o
EXE = ntemple
DISTDIR = .

all: $(EXE).tns

%.o: %.c
	$(GCC) $(GCCFLAGS) -c $< -o $@

$(EXE).elf: $(OBJS)
	mkdir -p $(DISTDIR)
	$(LD) $^ -o $@ $(LDFLAGS)

$(EXE).tns: $(EXE).elf
	$(GENZEHN) --input $^ --output $@.zehn $(ZEHNFLAGS)
	make-prg $@.zehn $@
	rm $@.zehn

preview:
	clang -O2 -o host/preview host/preview.c
	clang -O2 -o host/falltest host/falltest.c
	clang -O2 -o host/turntest host/turntest.c
	./host/preview prev
	./host/turntest prev
	./host/falltest prev
	python3 tools/shots.py prev
	@echo "frames written to prev/"

check:
	clang -O2 -o host/dirtest host/dirtest.c
	clang -O2 -o host/maptest host/maptest.c
	clang -O2 -o host/soak host/soak.c
	./host/dirtest
	./host/maptest
	./host/soak

clean:
	rm -f $(OBJS) $(DISTDIR)/$(EXE).tns $(DISTDIR)/$(EXE).elf $(DISTDIR)/$(EXE).zehn
