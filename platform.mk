PLAT ?= none
PLATS = linux freebsd macosx openwrt android

CC ?= gcc

.PHONY : none $(PLATS) clean all cleanall

#ifneq ($(PLAT), none)

.PHONY : default

default :
	$(MAKE) $(PLAT)

#endif

none :
	@echo "Please do 'make PLATFORM' where PLATFORM is one of these:"
	@echo "   $(PLATS)"

SKYNET_LIBS := -lpthread -lm
SHARED := -fPIC --shared
EXPORT := -Wl,-E

linux : PLAT = linux
macosx : PLAT = macosx
freebsd : PLAT = freebsd
openwrt : PLAT = openwrt
android : PLAT = android

macosx : SHARED := -fPIC -dynamiclib -Wl,-undefined,dynamic_lookup
macosx : EXPORT :=
macosx linux openwrt : SKYNET_LIBS += -ldl
linux freebsd openwrt : SKYNET_LIBS += -lrt
android : SKYNET_LIBS = -lm

# Turn off jemalloc and malloc hook on macosx

macosx : MALLOC_STATICLIB :=
macosx : SKYNET_DEFINES :=-DNOUSE_JEMALLOC
openwrt : MALLOC_STATICLIB :=
openwrt : SKYNET_DEFINES :=-DNOUSE_JEMALLOC
# openwrt : JEMALLOC_FLAGS :=--host=$(TOOLCHAIN_ARCH)-openwrt-linux
android : MALLOC_STATICLIB :=
android : SKYNET_DEFINES :=-DNOUSE_JEMALLOC

linux macosx freebsd openwrt android :
	$(MAKE) all PLAT=$@ SKYNET_LIBS="$(SKYNET_LIBS)" SHARED="$(SHARED)" EXPORT="$(EXPORT)" MALLOC_STATICLIB="$(MALLOC_STATICLIB)" SKYNET_DEFINES="$(SKYNET_DEFINES)" JEMALLOC_FLAGS="$(JEMALLOC_FLAGS)"
