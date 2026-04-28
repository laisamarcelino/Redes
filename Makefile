CC := gcc
CPPFLAGS := -I.
CFLAGS := -std=c11 -Wall -Wextra -Werror

TESTS_DIR := tests
BUILD_DIR := $(TESTS_DIR)/build
UNIT_SRC_DIR := $(TESTS_DIR)/unit
INTEGRATION_SRC_DIR := $(TESTS_DIR)/integration

APP_BINS := cliente servidor

UNIT_TEST_SRCS := $(shell find $(UNIT_SRC_DIR) -type f -name '*.c' 2>/dev/null | sort)
INTEGRATION_TEST_SRCS := $(shell find $(INTEGRATION_SRC_DIR) -type f -name '*.c' 2>/dev/null | sort)

UNIT_TEST_BINS := $(patsubst $(UNIT_SRC_DIR)/%.c,$(BUILD_DIR)/unit/%,$(UNIT_TEST_SRCS))
INTEGRATION_TEST_BINS := $(patsubst $(INTEGRATION_SRC_DIR)/%.c,$(BUILD_DIR)/integration/%,$(INTEGRATION_TEST_SRCS))
TEST_BINS := $(UNIT_TEST_BINS) $(INTEGRATION_TEST_BINS)

# Dependencias extras por teste de integracao.
# Formato da chave:
#   EXTRA_SRCS_integration_<caminho_relativo_com_barras_trocadas_por_underscore>
EXTRA_SRCS_integration_network_test_network := src/network.c
EXTRA_SRCS_integration_network_test_file_transfer := src/network.c

.PHONY: all tests build-tests test test-unit test-integration list-tests clean

all: $(APP_BINS)

cliente: src/cliente.c src/network.c include/network.h
	$(CC) $(CPPFLAGS) $(CFLAGS) src/cliente.c src/network.c -o $@

servidor: src/servidor.c src/network.c include/network.h
	$(CC) $(CPPFLAGS) $(CFLAGS) src/servidor.c src/network.c -o $@

tests: test

build-tests: $(TEST_BINS)

test: test-unit test-integration

test-unit: $(UNIT_TEST_BINS)
	@set -e; for test_bin in $(UNIT_TEST_BINS); do ./$$test_bin; done

test-integration: $(INTEGRATION_TEST_BINS)
	@set -e; for test_bin in $(INTEGRATION_TEST_BINS); do ./$$test_bin; done

list-tests:
	@printf 'Unit tests:\n'
	@for test_src in $(UNIT_TEST_SRCS); do printf '  %s\n' "$$test_src"; done
	@printf 'Integration tests:\n'
	@for test_src in $(INTEGRATION_TEST_SRCS); do printf '  %s\n' "$$test_src"; done

define build_test_rule
$1: EXTRA_SRCS := $3
$1: $2 $3
	@mkdir -p $$(dir $$@)
	$$(CC) $$(CPPFLAGS) $$(CFLAGS) $2 $$(EXTRA_SRCS) -o $$@
endef

$(foreach src,$(UNIT_TEST_SRCS),\
  $(eval $(call build_test_rule,\
    $(patsubst $(UNIT_SRC_DIR)/%.c,$(BUILD_DIR)/unit/%,$(src)),\
    $(src),\
    )))

$(foreach src,$(INTEGRATION_TEST_SRCS),\
  $(eval $(call build_test_rule,\
    $(patsubst $(INTEGRATION_SRC_DIR)/%.c,$(BUILD_DIR)/integration/%,$(src)),\
    $(src),\
    $(EXTRA_SRCS_integration_$(subst /,_,$(basename $(patsubst $(INTEGRATION_SRC_DIR)/%,%,$(src))))))))

clean:
	rm -rf $(BUILD_DIR) $(APP_BINS)
