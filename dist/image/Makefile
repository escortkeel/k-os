ROOTDIR ?= $(shell readlink -f ../..)
include $(ROOTDIR)/Makefile.config

.PHONY: all clean

.DELETE_ON_ERROR:

SRCDIR := root
OBJDIR := build

SRCS := $(shell find -L $(SRCDIR) -type f -name "*")
OBJS := $(patsubst $(SRCDIR)/%,$(OBJDIR)/%,$(SRCS))

all: $(IMAGE)

$(OBJDIR)/$(ROOTRAMFS): $(APPSDIR)/$(ROOTRAMFS)
	@echo "      cp  $< -> $@"
	@mkdir -p $(shell dirname $@)
	@cp -r $< $@

$(OBJDIR)/$(KBINARY): $(KERNELDIR)/$(KBINARY)
	@echo "      cp  $< -> $@"
	@mkdir -p $(shell dirname $@)
	@cp -r $< $@

$(OBJS): $(OBJDIR)/% : $(SRCDIR)/%
	@echo "      cp  $< -> $@"
	@mkdir -p $(shell dirname $@)
	@cp -r $< $@

$(IMAGE): $(OBJS) $(OBJDIR)/$(ROOTRAMFS) $(OBJDIR)/$(KBINARY)
	@echo "      mkimage"
	@grub-mkrescue -o $@ $(OBJDIR)

clean:
	rm -rf $(OBJDIR)
	rm -f $(IMAGE)