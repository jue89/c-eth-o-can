CC=gcc
CFLAGS=-Wall
SOURCES=crc16.c mode-emulated.c opts.c setup.c ethocan.c
OBJECTS=$(SOURCES:.c=.o)
EXECUTABLE=ethocan

OBJECTDIR=obj/
SRCDIR=src/
COBJECTS=$(addprefix $(OBJECTDIR),$(OBJECTS))

.PHONY: all
all: $(EXECUTABLE)

.PHONY: clean
clean:
	rm $(COBJECTS) $(EXECUTABLE)

$(EXECUTABLE): $(COBJECTS)
	$(CC) $(COBJECTS) $(LDFLAGS) -o $@

$(OBJECTDIR)%.o: $(SRCDIR)%.c
	$(CC) $(CFLAGS) -c $< -o $@
