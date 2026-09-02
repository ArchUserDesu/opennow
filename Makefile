# OpenNOW-Xenon LibXenon build
.SUFFIXES:

.PHONY: host-test check-xenon-env fetch-upstream assemble-port xenon clean
host-test:
	cmake -S . -B build -DOPENNOW_HOST_TESTS=ON
	cmake --build build -j
	ctest --test-dir build --output-on-failure

check-xenon-env:
	./scripts/check-xenon-env.sh
fetch-upstream:
	./scripts/fetch-upstream.sh

assemble-port: fetch-upstream
	./scripts/assemble-port.sh

xenon: check-xenon-env
	$(MAKE) -f Makefile.xenon

clean:
	rm -rf build build-xenon OpenNOW-Xenon.elf OpenNOW-Xenon.elf32
