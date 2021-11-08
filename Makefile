update: osx linux push
	@echo "all artifacts are updated"
run: build
	docker run -it --rm -v `pwd`:/home:Z wbsouza/zetasql-formatter:latest
build:
	DOCKER_BUILDKIT=1 docker build -t wbsouza/zetasql-formatter:latest -f ./docker/Dockerfile .
build-formatter: build
	mv ./zetasql-kotlin/build/*_jar.jar ~/.Trash/
	docker run -it --rm -v `pwd`:/work/zetasql/ \
		-v /var/run/docker.sock:/var/run/docker.sock \
		bazel
push: build
	docker push wbsouza/zetasql-formatter:latest
osx:
	CC=g++ bazelisk build //zetasql/tools/zetasql-formatter:format
	sudo cp ./bazel-bin/zetasql/tools/zetasql-formatter/format ./bin/osx/zetasql-formatter
	sudo cp ./bin/osx/zetasql-formatter /usr/local/bin
linux: build
	./docker/linux-copy-bin.sh
test:
		CC=g++ bazelisk test --test_output=errors //zetasql/tools/...
.PHONY: run build build-formatter osx push test
