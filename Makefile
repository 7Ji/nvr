C_IMPLEMENTATIONS=$(wildcard c*)
# C_BINARIES=$(patsubst %,%/nvr,$(C_IMPLEMENTATIONS))

.PHONY: clean c* *nvr

all: $(C_IMPLEMENTATIONS)

$(C_IMPLEMENTATIONS): c%:
	make -C $@

clean: $(patsubst %,clean_%,$(C_IMPLEMENTATIONS))

clean_%:
	make -C $(patsubst clean_%,%,$@) clean