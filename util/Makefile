ROOTPATH ?= $(shell readlink -f ..)
include $(ROOTPATH)/Makefile.config

.PHONY: all clean

.DELETE_ON_ERROR:

SRCDIR := src
OBJDIR := $(UTILBINPATH)

CFLAGS := -Wall -Wextra -Werror -Wno-unused-parameter -std=gnu99 -g -O3

SRCS := $(shell find -L $(SRCDIR) -type f -name "*.c")
OBJS := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%,$(SRCS))

all: $(OBJS)

$(OBJS): $(OBJDIR)/% : $(SRCDIR)/%.c
	@echo "      cc  $(patsubst $(SRCDIR)/%,%,$<)"
	@mkdir -p $(shell dirname $@)
	@$(CC) $(CFLAGS) -o $@ $<

clean:
	@rm -rf $(OBJDIR)
