target: app

app: RedisConnect.h RedisCommand.cpp
	g++ -std=c++11 -pthread -lutil -lbsd -ldl -lm -o redis RedisCommand.cpp

clean:
	@rm redis
