# RetroAchievements: rcheevos C sources and include paths, which fall outside
# the Makefile's support/*/*.cpp glob. Source list matches odelot/Main_MiSTer.
RCHEEVOS_DIR = lib/rcheevos

C_SRC += \
	$(RCHEEVOS_DIR)/src/rc_client.c \
	$(RCHEEVOS_DIR)/src/rc_compat.c \
	$(RCHEEVOS_DIR)/src/rc_util.c \
	$(RCHEEVOS_DIR)/src/rcheevos/alloc.c \
	$(RCHEEVOS_DIR)/src/rcheevos/condition.c \
	$(RCHEEVOS_DIR)/src/rcheevos/condset.c \
	$(RCHEEVOS_DIR)/src/rcheevos/consoleinfo.c \
	$(RCHEEVOS_DIR)/src/rcheevos/format.c \
	$(RCHEEVOS_DIR)/src/rcheevos/lboard.c \
	$(RCHEEVOS_DIR)/src/rcheevos/memref.c \
	$(RCHEEVOS_DIR)/src/rcheevos/operand.c \
	$(RCHEEVOS_DIR)/src/rcheevos/rc_validate.c \
	$(RCHEEVOS_DIR)/src/rcheevos/richpresence.c \
	$(RCHEEVOS_DIR)/src/rcheevos/runtime.c \
	$(RCHEEVOS_DIR)/src/rcheevos/runtime_progress.c \
	$(RCHEEVOS_DIR)/src/rcheevos/trigger.c \
	$(RCHEEVOS_DIR)/src/rcheevos/value.c \
	$(RCHEEVOS_DIR)/src/rapi/rc_api_common.c \
	$(RCHEEVOS_DIR)/src/rapi/rc_api_info.c \
	$(RCHEEVOS_DIR)/src/rapi/rc_api_runtime.c \
	$(RCHEEVOS_DIR)/src/rapi/rc_api_user.c \
	$(RCHEEVOS_DIR)/src/rhash/md5.c \
	$(RCHEEVOS_DIR)/src/rhash/hash.c \
	$(RCHEEVOS_DIR)/src/rhash/hash_disc.c \
	$(RCHEEVOS_DIR)/src/rhash/hash_encrypted.c \
	$(RCHEEVOS_DIR)/src/rhash/hash_rom.c \
	$(RCHEEVOS_DIR)/src/rhash/hash_zip.c \
	$(RCHEEVOS_DIR)/src/rhash/cdreader.c \
	$(RCHEEVOS_DIR)/src/rhash/aes.c

DFLAGS += -I./$(RCHEEVOS_DIR)/include -I./$(RCHEEVOS_DIR)/src -I./$(RCHEEVOS_DIR)/src/rcheevos -DHAS_RCHEEVOS=1
