client:
	gcc ./main.c ./logger/logger.c -o test-client

client-debug:
	gcc -g ./main.c ./logger/logger.c -o test-client

exec:
	cp ./test-client /tmp/
	rm ./test-client
	sudo python ./mininet-test.py

run: client exec

clean:
	rm ./test-client
