c: c/nvr
	make -C c

cpp: cpp/nvr
	make -C cpp

all: c cpp

.PHONY: clean

clean:
	rm -f c{,pp}/nvr{,.o}