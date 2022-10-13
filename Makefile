FIND_CMD := find . \( -path ./client \
-o -path ./protocol \
-o -path ./routing \
-o -path ./utils \) \
-o -type f -name
BUILD_FILES := $(shell $(FIND_CMD) BUILD | fgrep BUILD)
H_FILES := $(shell $(FIND_CMD) '*.h' | fgrep .h)
C_FILES := $(shell $(FIND_CMD) '*.c' | fgrep .c)

bgp-client:
	bazel build //client/bgp 

bgp-mininet-exec:
	cp bazel-bin/client/bgp/bgp /tmp/test-client
	sudo python ./mininet-test.py

run-mininet: bgp-client bgp-mininet-exec

run-bgp: bgp-client
	bazel-bin/client/bgp/bgp

clean:
	# git clean -fdx

fmt:
	buildifier $(BUILD_FILES)
	clang-format -i $(H_FILES)
	clang-format -i $(C_FILES)
