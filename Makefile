CFLAGS = -Wall -g
CC     = gcc $(CFLAGS)
LIBS 	 = -pthread -lpthread

target: bl_server bl_client

bl_server : util.o simpio.o server_funcs.o bl_server.o
	$(CC) -o bl_server util.o simpio.o server_funcs.o bl_server.o $(LIBS)
	@echo bl_server is ready

bl_client : util.o simpio.o server_funcs.o bl_client.o
	$(CC) -o bl_client util.o simpio.o server_funcs.o bl_client.o $(LIBS)
	@echo bl_client is ready

bl_server.o : bl_server.c blather.h
	$(CC) -c bl_server.c

bl_client.o : bl_client.c blather.h
	$(CC) -c bl_client.c

server_funcs.o : server_funcs.c simpio.c blather.h
	$(CC) -c $<

simpio.o : simpio.c blather.h
	$(CC) -c $<

simpio_demo.o : simpio_demo.c blather.h
	$(CC) -c $<

util.o : util.c blather.h
	$(CC) -c $<

clean :
	rm -f bl_server bl_client *.o *.fifo

## TEST TARGETS
TEST_PROGRAMS = test_blather.sh test_blather_data.sh normalize.awk cat_sig.sh filter-semopen-bug.awk

test : test-blather

make test-blather : bl_client bl_server $(TEST_PROGRAMS)
	chmod u+rx $(TEST_PROGRAMS)
	./test_blather.sh

clean-tests :
	cd test-data && \
	rm -f *.*
