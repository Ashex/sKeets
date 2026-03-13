.PHONY: build clean

build:
	./docker-build.sh

clean:
	docker run --rm -v "$(CURDIR)/build-kobo:/build" ubuntu:24.04 rm -rf /build/* /build/.[!.]* 2>/dev/null; \
	rm -rf build-kobo
