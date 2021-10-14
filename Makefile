SRCIDIR := ./inc
SRCSDIR := ./src
DEPDIR = ./dep

CFLAGS := -I$(SRCIDIR) -I$(DEPDIR)

DEPSRC := $(wildcard $(DEPDIR)/*.c)
DEPOBJ := $(DEPSRC:$(DEPDIR)/%.c=$(DEPDIR)/%.o)
SRC := $(wildcard $(SRCSDIR)/*.c)
OBJ := $(SRC:$(SRCSDIR)/%.c=$(SRCSDIR)/%.o)

CC := gcc
EXE := priority_cache


ifeq ($(DEBUG),yes)
 	OPTIMIZE_FLAG := -ggdb3 -DDEBUG
else ifeq ($(NOOP),yes)
 	OPTIMIZE_FLAG :=  
else
	OPTIMIZE_FLAG := -O2 -DNDEBUG
endif



all: $(EXE)

$(EXE): $(DEPOBJ)  $(OBJ) example_main.c 
	$(CC) $(CFLAGS) $(OPTIMIZE_FLAG) $^ -lm  -o $@

$(SRCSDIR)/%.o: $(SRCSDIR)/%.c
	$(CC) $(CFLAGS) $(OPTIMIZE_FLAG) -c $< -o $@

$(DEPDIR)/%.o: $(DEPDIR)/%.c
	$(CC) $(CFLAGS) $(OPTIMIZE_FLAG) -c $< -o $@


clean:
	rm -f priority_cache

	rm -f $(DEPDIR)/*.o $(SRCSDIR)/*.o
