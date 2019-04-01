CFLAGS := -Wall --std=gnu99 -g3 -Werror -fPIC -Wfatal-errors
#ASAN_FLAGS = -fsanitize=address -fno-omit-frame-pointer -fno-common
ASAN_FLAGS = -fsanitize=thread
ASAN_LIBS = -static-libasan

FILES_TO_CHECK := network.c network.h rcserver.c client.c rcserver.h client.h common.h common.c

ARCH := $(shell uname)
ifneq ($(ARCH),Darwin)
  LDFLAGS += -lpthread -lrt -static-libasan
endif

all: clean all_asan all_noasan

all_asan: clean rcserver client

all_noasan: clean rcserver_noasan client_noasan

rcserver: rcserver.o common.o steque.o network.o
	$(CC) -o $@ $(CFLAGS) $(ASAN_FLAGS) $(CURL_CFLAGS) $^ $(LDFLAGS) $(CURL_LIBS) $(ASAN_LIBS)

client: client.o common.o steque.o network.o
	$(CC) -o $@ $(CFLAGS) $(ASAN_FLAGS) $^ $(LDFLAGS) $(ASAN_LIBS)

rcserver_noasan: rcserver_noasan.o common_noasan.o steque_noasan.o network_noasan.o
	$(CC) -o $@ $(CFLAGS) $(CURL_CFLAGS) $^ $(LDFLAGS) $(CURL_LIBS)

client_noasan: client_noasan.o common_noasan.o steque_noasan.o network_noasan.o
	$(CC) -o $@ $(CFLAGS) $^ $(LDFLAGS)

%_noasan.o : %.c
	$(CC) -c -o $@ $(CFLAGS) $<

%.o : %.c
	$(CC) -c -o $@ $(CFLAGS) $(ASAN_FLAGS) $<

.PHONY: clean

clean:
	rm -rf *.o rcserver client rcserver_noasan client_noasan

fix:                                                                                                
	my_checkpatch.pl -fix-inplace -terse -file -no-tree --max-line-length=120 $(FILES_TO_CHECK) 
                                                                                                    
style:                                                                                              
	astyle -t --style=linux -n $(FILES_TO_CHECK) 

check:
	my_checkpatch.pl -terse --show-types -file -no-tree --ignore AVOID_EXTERNS --ignore SYMBOLIC_PERMS --ignore CONST_STRUCT --ignore SSCANF_TO_KSTRTO --ignore BRACES --max-line-length=120 $(FILES_TO_CHECK)
