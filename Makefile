CC          := gcc
CFLAGS      := -Wall -c
LDFLAGS     := -Wall -pthread
SOURCES     := $(wildcard *.c)
MAINOBJECTS := $(subst .c,.o,$(shell grep -l main $(SOURCES)))
ALL         := $(subst .o,,$(MAINOBJECTS))
DEPENDS     := $(subst .c,.d,$(SOURCES))
ALLOBJECTS  := $(subst .c,.o,$(SOURCES))
OBJECTS	    := $(filter-out $(MAINOBJECTS),$(ALLOBJECTS)) 

all: $(DEPENDS) $(ALL)

$(DEPENDS) : %.d : %.c
	$(CC) -MM $< > $@
	@echo -e: "\t"$(CC) $(CFLAGS) $< >> $@

$(ALL) : % : %.o $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^

-include $(DEPENDS)

clean:
	-rm -f *.o $(ALL) $(ALLOBJECTS) $(DEPENDS)
