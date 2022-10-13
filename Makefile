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
