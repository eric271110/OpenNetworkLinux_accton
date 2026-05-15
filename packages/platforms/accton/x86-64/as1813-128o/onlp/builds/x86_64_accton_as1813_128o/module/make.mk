###############################################################################
#
#
#
###############################################################################
THIS_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
x86_64_accton_as1813_128o_INCLUDES := -I $(THIS_DIR)inc
x86_64_accton_as1813_128o_INTERNAL_INCLUDES := -I $(THIS_DIR)src
x86_64_accton_as1813_128o_DEPENDMODULE_ENTRIES := init:x86_64_accton_as1813_128o ucli:x86_64_accton_as1813_128o
