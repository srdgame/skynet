CC	:= gcc
LD      := ld
CFLAGS 	:= -O2 -I../include/ -fPIC
LDFLAGS := -shared -fPIC 

all : fake_sdk.so

OBJS := vsdk_api.o

%.o:%.c
	$(CC) -c $(CFLAGS)  $< -o $*.o

fake_sdk.so : $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

.PHONY: clean

clean: 
	rm -f *.o
	rm -f fake_sdk.so
