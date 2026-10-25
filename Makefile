# Build:  make        Clean:  make clean

all: server client

server: server.c bank_fun.h common_functions.h customer_functions.h admin_functions.h employee_functions.h manager_functions.h
	gcc server.c -o server -lcrypt -lpthread

client: client.c
	gcc client.c -o client

clean:
	rm -f server client
