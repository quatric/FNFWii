#---------------------------------------------------------------------------------
# FNFWii
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=<path to>devkitPPC")
endif

include $(DEVKITPPC)/wii_rules

TARGET   := boot
BUILD    := build
SOURCES  := source source/cjson libwiiinput/source
DATA     :=
INCLUDES := source source/cjson libwiiinput/include

APP_VERSION ?= 0.1.3

CFLAGS   = -g -O2 -Wall -Wno-unused-function $(MACHDEP) $(INCLUDE) \
           -DAPP_VERSION=\"$(APP_VERSION)\" -DHAVE_GCADAPTER -DHAVE_WPADQUEUE
CXXFLAGS = $(CFLAGS)
LDFLAGS  = -g $(MACHDEP) -Wl,-Map,$(notdir $@).map -Wl,--wrap=wiiuse_init_cmd_queue

LIBS     := -lwiiupdate -lvorbisidec -logg -lfat -lasnd -lwiikeyboard -lwiiuse -lbte -logc -lm

LIBDIRS  := $(PORTLIBS) $(CURDIR)/libwiiupdate

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT  := $(CURDIR)/$(TARGET)
export VPATH   := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
export LD := $(CC)
export OFILES := $(CFILES:.c=.o)
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(LIBOGC_INC)
export LIBPATHS := -L$(LIBOGC_LIB) $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export OUTPUT

.PHONY: $(BUILD) clean dist libwiiupdate-lib

$(BUILD): libwiiupdate-lib
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

libwiiupdate-lib:
	@$(MAKE) --no-print-directory -C libwiiupdate

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(OUTPUT).elf $(OUTPUT).dol dist

# sd-card layout: dist/apps/FNFWii/{boot.dol,meta.xml,icon.png,data/}
dist: $(BUILD)
	@mkdir -p dist/apps/FNFWii
	@cp $(OUTPUT).dol dist/apps/FNFWii/boot.dol
	@sed "s/@VERSION@/$(APP_VERSION)/" apps/FNFWii/meta.xml > dist/apps/FNFWii/meta.xml
	@[ ! -f apps/FNFWii/icon.png ] || cp apps/FNFWii/icon.png dist/apps/FNFWii/
	@[ ! -d data_out ] || rsync -a --delete data_out/ dist/apps/FNFWii/data/

#---------------------------------------------------------------------------------
else

DEPENDS := $(OFILES:.o=.d)

$(OUTPUT).dol: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif
