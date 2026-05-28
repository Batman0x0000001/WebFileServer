CXX ?= g++
CXXFLAGS ?= -std=c++11 -Wall -Wextra -MMD -MP
MYSQL_CONFIG ?= $(shell if [ -x /usr/bin/mysql_config ]; then echo /usr/bin/mysql_config; else command -v mysql_config; fi)
MYSQL_CFLAGS ?= $(shell $(MYSQL_CONFIG) --cflags 2>/dev/null)
MYSQL_LIBS ?= $(shell $(MYSQL_CONFIG) --libs 2>/dev/null)
HIREDIS_CFLAGS ?= $(shell pkg-config --cflags hiredis 2>/dev/null)
HIREDIS_LIBS ?= $(shell pkg-config --libs hiredis 2>/dev/null)
OPENSSL_LIBS ?= -lcrypto

COMMON_SRC = server/server.cpp threadpool/threadpool.cpp event/event_handler.cpp event/connection_manager.cpp event/epoll_util.cpp request/request_processor.cpp response/response_sender.cpp response/action_response_builder.cpp utils/log.cpp utils/string_utils.cpp utils/encoding.cpp utils/http_helpers.cpp fileservice/file_service.cpp fileservice/file_query_service.cpp auth/auth.cpp auth/auth_actions.cpp config/config.cpp database/mysqlclient.cpp database/mysqlclient_user.cpp database/mysqlclient_file.cpp cache/redisclient.cpp message/http_parser.cpp router/request_router.cpp response/response_builder.cpp upload/multipart_upload.cpp
MAIN_OBJ = main.o $(COMMON_SRC:.cpp=.o)
TEST_OBJ = tests/http_parser_test.o message/http_parser.o utils/string_utils.o utils/encoding.o utils/http_helpers.o auth/auth.o config/config.o database/mysqlclient.o database/mysqlclient_user.o database/mysqlclient_file.o cache/redisclient.o utils/log.o
MULTIPART_TEST_OBJ = tests/multipart_upload_test.o upload/multipart_upload.o fileservice/file_service.o utils/string_utils.o utils/http_helpers.o utils/encoding.o config/config.o database/mysqlclient.o database/mysqlclient_user.o database/mysqlclient_file.o cache/redisclient.o utils/log.o

fileserver: main

main: $(MAIN_OBJ)
	$(CXX) $(CXXFLAGS) $(MYSQL_CFLAGS) $(HIREDIS_CFLAGS) $^ -lpthread $(MYSQL_LIBS) $(HIREDIS_LIBS) $(OPENSSL_LIBS) -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(MYSQL_CFLAGS) $(HIREDIS_CFLAGS) -c $< -o $@

tests/http_parser_test: $(TEST_OBJ)
	$(CXX) $(CXXFLAGS) $(MYSQL_CFLAGS) $(HIREDIS_CFLAGS) $^ $(MYSQL_LIBS) $(HIREDIS_LIBS) $(OPENSSL_LIBS) -o $@

tests/multipart_upload_test: $(MULTIPART_TEST_OBJ)
	$(CXX) $(CXXFLAGS) $(MYSQL_CFLAGS) $(HIREDIS_CFLAGS) $^ $(MYSQL_LIBS) $(HIREDIS_LIBS) $(OPENSSL_LIBS) -o $@

test: tests/http_parser_test tests/multipart_upload_test
	./tests/http_parser_test
	./tests/multipart_upload_test

clean:
	rm  -f main tests/http_parser_test tests/multipart_upload_test $(MAIN_OBJ) $(MAIN_OBJ:.o=.d) $(TEST_OBJ) $(TEST_OBJ:.o=.d) $(MULTIPART_TEST_OBJ) $(MULTIPART_TEST_OBJ:.o=.d)

-include $(MAIN_OBJ:.o=.d) $(TEST_OBJ:.o=.d) $(MULTIPART_TEST_OBJ:.o=.d)
