OBJS=postgres.o

CPPFLAGS=-D_OS_LINUX -D_CC_GCC  -fPIC

libpgclient.so: $(OBJS)
	g++ -shared -o $@ $< -lpq


clean:
	rm libpgclient.so postgres.o
