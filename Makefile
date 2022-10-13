bgp-client:
	bazel build //client/bgp 

bgp-exec:
	cp bazel-bin/client/bgp/bgp /tmp/test-client
	sudo python ./mininet-test.py

run: bgp-client bgp-exec

clean:
	# git clean -fdx
