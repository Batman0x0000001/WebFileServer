CXX ?= g++
MYSQL_CONFIG ?= $(shell if [ -x /usr/bin/mysql_config ]; then echo /usr/bin/mysql_config; else command -v mysql_config; fi)
MYSQL_CFLAGS ?= $(shell $(MYSQL_CONFIG) --cflags 2>/dev/null)
MYSQL_LIBS ?= $(shell $(MYSQL_CONFIG) --libs 2>/dev/null)
HIREDIS_CFLAGS ?= $(shell pkg-config --cflags hiredis 2>/dev/null)
HIREDIS_LIBS ?= $(shell pkg-config --libs hiredis 2>/dev/null)

fileserver: main.cpp ./fileserver/fileserver.cpp ./threadpool/threadpool.cpp ./event/myevent.cpp ./utils/utils.cpp ./utils/encoding.cpp ./auth/auth.cpp ./config/config.cpp ./database/mysqlclient.cpp ./cache/redisclient.cpp
	$(CXX) -std=c++11 -Wall -Wextra $(MYSQL_CFLAGS) $(HIREDIS_CFLAGS) $^ -lpthread $(MYSQL_LIBS) $(HIREDIS_LIBS) -o main

clean:
	rm  -r main
