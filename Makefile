
# set V=0 # verbose mode 0/1 (off/on)
DEFAULT_VERBOSE = 0

ifeq ($(V), 1)
  Q =
  S =
else
  Q = @
  S = -s
endif

V_CC    = $(_v_cc_$(V))
_v_cc_  = $(_v_cc_$(DEFAULT_VERBOSE))
_v_cc_0 = @echo "    CC    " $@ ;
_v_cc_1 =

V_AR    = $(_v_ar_$(V))
_v_ar_  = $(_v_ar_$(DEFAULT_VERBOSE))
_v_ar_0 = @echo "    AR    " $@ ;
_v_ar_1 =

V_LD    = $(_v_ld_$(V))
_v_ld_  = $(_v_ld_$(DEFAULT_VERBOSE))
_v_ld_0 = @echo "    LD    " $@ ;
_v_ld_1 =

V_SO    = $(_v_so_$(V))
_v_so_  = $(_v_so_$(DEFAULT_VERBOSE))
_v_so_0 = @echo "    SO    " $@ ;
_v_so_1 =

V_YACC    = $(_v_yacc_$(V))
_v_yacc_  = $(_v_yacc_$(DEFAULT_VERBOSE))
_v_yacc_0 = @echo "  YACC    " $@ ;
_v_yacc_1 =

V_LEX   = $(_v_lex_$(V))
_v_lex_  = $(_v_lex_$(DEFAULT_VERBOSE))
_v_lex_0 = @echo "   LEX    " $@ ;
_v_lex_1 =

V_JAVAC    = $(_v_javac_$(V))
_v_javac_  = $(_v_javac_$(DEFAULT_VERBOSE))
_v_javac_0 = @echo " JAVAC    " $@ ;
_v_javac_1 =


OS := $(shell uname)

CFLAGS :=-g -O2
ifeq ($(OS), Darwin)
CFLAGS += -DUSE_GCC_BUILTIN_ATOMIC=1
endif

CC=gcc
LD=$(CC)
AR=ar

INCLUDES=-I$(SRC_DIR)
DEFS=
#DEFS=-DUSING_PTHREAD_MUTEX_ONLY_INSERT

LDFLAGS=-L$(LIB_DIR)
LD_LIBS=-lc -lm -lpthread

SRC_DIR=./src
OBJ_DIR=./obj
BIN_DIR=./bin
LIB_DIR=./lib
DIRS=$(SRC_DIR) $(OBJ_DIR) $(BIN_DIR) $(LIB_DIR)

define CC_cmd
	@ mkdir -p $(dir $@);
	$(V_CC) $(CC) $(DEFS) $(CFLAGS) $(INCLUDES) -c $< -o $@;
endef

define LD_cmd
	@ mkdir -p $(dir $@);
	$(V_LD) $(LD) $(LDFLAGS) -o $@ $? $(LD_LIBS)
endef

define AR_cmd
	@ mkdir -p $(dir $@);
	$(V_AR) $(AR) -ruv $@ $?
endef


LIB_SRCS = $(SRC_DIR)/lock_free_dlist.c         \
					 $(SRC_DIR)/util.c              \
					 $(SRC_DIR)/atomic.c            \
					 $(SRC_DIR)/rand_r.c

LIB_OBJS = $(LIB_SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

TEST_SRCS = $(SRC_DIR)/lf_dlist_test.c
TEST_OBJS = $(TEST_SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
TEST_BINS = $(TEST_SRCS:$(SRC_DIR)/%.c=$(BIN_DIR)/%)
TEST_LDFLAGS = $(LDFLAGS) -lc -lm -lpthread -llflist -L./lib

OBJS = $(LIB_OBJS) $(TEST_OBJS)
LIBS = $(LIB_DIR)/liblflist.a
BINS = $(TEST_BINS)

all: mkdirs
	$(Q) $(MAKE) build

build_test: debug $(TEST_OBJS)
	$(Q) $(LD) $(TEST_OBJS) -o $(TEST_BINS) $(TEST_LDFLAGS) 

test: build_test
	$(Q) cd $(BIN_DIR) && $(SHELL) test_suite.sh

test_time: build_test
	$(Q) cd $(BIN_DIR) && PRINT_ELAPSED_TIME=1 $(SHELL) test_suite.sh

debug: 
	$(Q) $(MAKE) CFLAGS='$(CFLAGS) -g -O0' build 

build: $(LIB_OBJS)
	$(Q) $(MAKE) libs

libs: $(LIB_OBJS)
	$(Q) $(MAKE) $(LIBS)

mkdirs:
	$(Q) mkdir -p $(DIRS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC_cmd)

$(BIN_DIR)/%: $(OBJ_DIR)/%.o
	$(LD_cmd)

$(LIB_DIR)/%.a: $(LIB_OBJS)
	$(AR_cmd)

clean:
	rm -f $(BINS) $(OBJS) $(LIBS)

