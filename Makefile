PROGRAM = main

EXTRA_COMPONENTS = \
	extras/http-parser \
	$(abspath esp-wolfssl) \
	$(abspath esp-cjson) \
	$(abspath esp-homekit)

FLASH_SIZE ?= 8
HOMEKIT_SPI_FLASH_BASE_ADDR ?= 0x8C000

EXTRA_CFLAGS += -I../.. -DHOMEKIT_SHORT_APPLE_UUIDS

include $(SDK_PATH)/common.mk

LIBS += m

monitor:
	$(FILTEROUTPUT) --port $(ESPPORT) --baud $(ESPBAUD) --elf $(PROGRAM_OUT)
