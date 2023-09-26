#ifndef REDIS_CONNECT_H
#define REDIS_CONNECT_H
///////////////////////////////////////////////////////////////
#include <map>
#include <mutex>
#include <memory>
#include <vector>
#include <string>
#include <sstream>
#include <cstring>

#ifdef _MSC_VER

#include <conio.h>
#include <windows.h>

#pragma comment(lib, "WS2_32.lib")

#else

#include <errno.h>
#include <netdb.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/statfs.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define ioctlsocket ioctl
#define INVALID_SOCKET (SOCKET)(-1)

typedef int SOCKET;

#endif

using namespace std;

class RedisConnect
{
	typedef std::mutex Mutex;
	typedef std::lock_guard<mutex> Locker;

	friend class Command;

public:
	static const int OK = 1;
	static const int FAIL = -1;
	static const int IOERR = -2;
	static const int SYSERR = -3;
	static const int NETERR = -4;
	static const int TIMEOUT = -5;
	static const int DATAERR = -6;
	static const int SYSBUSY = -7;
	static const int PARAMERR = -8;
	static const int NOTFOUND = -9;
	static const int NETCLOSE = -10;
	static const int NETDELAY = -11;
	static const int AUTHFAIL = -12;
	static const int POOL_MAXLEN = 8;
	static const int SOCKET_TIMEOUT = 10;

public:
	class Socket
	{
	protected:
		SOCKET sock = INVALID_SOCKET;

	public:
		static bool IsSocketTimeout()
		{
#ifndef _MSC_VER
			return errno == 0 || errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#else
			return WSAGetLastError() == WSAETIMEDOUT;
#endif
		}
		static void SocketClose(SOCKET sock)
		{
			if (IsSocketClosed(sock)) return;

#ifndef _MSC_VER
			::close(sock);
#else
			::closesocket(sock);
#endif
		}
		static bool IsSocketClosed(SOCKET sock)
		{
			return sock == INVALID_SOCKET || sock < 0;
		}
		static bool SocketSetSendTimeout(SOCKET sock, int ms)
		{
#ifndef _MSC_VER
			struct timeval tv;

			tv.tv_sec = ms / 1000;
			tv.tv_usec = ms % 1000 * 1000;

			return setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)(&tv), sizeof(tv)) == 0;
#else
			return setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)(&ms), sizeof(ms)) == 0;
#endif
		}
		static bool SocketSetRecvTimeout(SOCKET sock, int ms)
		{
#ifndef _MSC_VER
			struct timeval tv;

			tv.tv_sec = ms / 1000;
			tv.tv_usec = ms % 1000 * 1000;

			return setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)(&tv), sizeof(tv)) == 0;
#else
			return setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)(&ms), sizeof(ms)) == 0;
#endif
		}
		SOCKET SocketConnectTimeout(const char* ip, int port, double timeout)
		{
			u_long mode = 1;
			struct timeval tv;
			struct sockaddr_in addr;
			SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);

			if (IsSocketClosed(sock)) return INVALID_SOCKET;
			
			long ms = (long)(timeout * 1000 + 0.5);

			tv.tv_sec = ms / 1000;
			tv.tv_usec = ms % 1000;
			addr.sin_family = AF_INET;
			addr.sin_port = htons(port);
			addr.sin_addr.s_addr = inet_addr(ip);

			ioctlsocket(sock, FIONBIO, &mode); mode = 0;

			if (::connect(sock, (struct sockaddr*)(&addr), sizeof(addr)) == 0)
			{
				ioctlsocket(sock, FIONBIO, &mode);

				return sock;
			}

#ifndef _MSC_VER
			struct epoll_event ev;
			struct epoll_event evs;
			int handle = epoll_create(1);

			if (handle < 0)
			{
				SocketClose(sock);
			
				return INVALID_SOCKET;
			}
			
			memset(&ev, 0, sizeof(ev));
			
			ev.events = EPOLLOUT | EPOLLERR | EPOLLHUP;
			
			epoll_ctl(handle, EPOLL_CTL_ADD, sock, &ev);
			
			if (epoll_wait(handle, &evs, 1, ms) > 0)
			{
				if (evs.events & EPOLLOUT)
				{
					int res = FAIL;
					socklen_t len = sizeof(res);
			
					getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)(&res), &len);
					ioctlsocket(sock, FIONBIO, &mode);
			
					if (res == 0)
					{
						::close(handle);
			
						return sock;
					}
				}
			}
			
			::close(handle);
#else
			fd_set ws;
			FD_ZERO(&ws);
			FD_SET(sock, &ws);

			if (select(sock + 1, NULL, &ws, NULL, &tv) > 0)
			{
				int res = ERROR;
				int len = sizeof(res);
			
				getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)(&res), &len);
				ioctlsocket(sock, FIONBIO, &mode);
			
				if (res == 0) return sock;
			}
#endif

			SocketClose(sock);
			
			return INVALID_SOCKET;
		}

	public:
		void close()
		{
			SocketClose(sock);
			sock = INVALID_SOCKET;
		}
		bool setSendTimeout(int ms)
		{
			return SocketSetSendTimeout(sock, ms);
		}
		bool setRecvTimeout(int ms)
		{
			return SocketSetRecvTimeout(sock, ms);
		}
		bool connect(const string& ip, int port, double timeout)
		{
			close();

			sock = SocketConnectTimeout(ip.c_str(), port, timeout);

			return IsSocketClosed(sock) ? false : true;
		}

	public:
		int write(const void* data, int count)
		{
			const char* str = (const char*)(data);

			int num = 0;
			int times = 0;
			int writed = 0;

			while (writed < count)
			{
				if ((num = send(sock, str + writed, count - writed, 0)) > 0)
				{
					if (num > 8)
					{
						times = 0;
					}
					else
					{
						if (++times > 100) return TIMEOUT;
					}

					writed += num;
				}
				else
				{
					if (IsSocketTimeout())
					{
						if (++times > 100) return TIMEOUT;

						continue;
					}

					return NETERR;
				}
			}

			return writed;
		}
		int read(void* data, int count, bool completed)
		{
			char* str = (char*)(data);

			if (completed)
			{
				int num = 0;
				int times = 0;
				int readed = 0;

				while (readed < count)
				{
					if ((num = recv(sock, str + readed, count - readed, 0)) > 0)
					{
						if (num > 8)
						{
							times = 0;
						}
						else
						{
							if (++times > 100) return TIMEOUT;
						}

						readed += num;
					}
					else if (num == 0)
					{
						return NETCLOSE;
					}
					else
					{
						if (IsSocketTimeout())
						{
							if (++times > 100) return TIMEOUT;

							continue;
						}

						return NETERR;
					}
				}

				return readed;
			}
			else
			{
				int val = recv(sock, str, count, 0);

				if (val > 0) return val;

				if (val == 0) return NETCLOSE;

				if (IsSocketTimeout()) return 0;

				return NETERR;
			}
		}
	};

	class Command
	{
		friend RedisConnect;

	protected:
		int status;
		string msg;
		vector<string> res;
		vector<string> vec;

	protected:
		int parse(const char* msg, int len)
		{
			if (*msg == '$')
			{
				const char* end = parseNode(msg, len);

				if (end == NULL) return DATAERR;

				switch (end - msg)
				{
				case 0: return TIMEOUT;
				case -1: return NOTFOUND;
				}

				return OK;
			}

			const char* str = msg + 1;
			const char* end = strstr(str, "\r\n");

			if (end == NULL) return TIMEOUT;

			if (*msg == '+' || *msg == '-' || *msg == ':')
			{
				this->status = OK;
				this->msg = string(str, end);

				if (*msg == '+') return OK;
				if (*msg == '-') return FAIL;

				this->status = atoi(str);

				return OK;
			}

			if (*msg == '*')
			{
				int cnt = atoi(str);
				const char* tail = msg + len;

				vec.clear();
				str = end + 2;

				while (cnt > 0)
				{
					end = parseNode(str, tail - str);

					if (end == NULL) return DATAERR;
					if (end == str) return TIMEOUT;

					str = end;
					cnt--;
				}

				return res.size();
			}

			return DATAERR;
		}
		const char* parseNode(const char* msg, int len)
		{
			const char* str = msg + 1;
			const char* end = strstr(str, "\r\n");

			if (end == NULL) return msg;

			int sz = atoi(str);

			if (sz < 0) return msg + sz;

			str = end + 2;
			end = str + sz + 2;

			if (msg + len < end) return msg;

			res.push_back(string(str, str + sz));

			return end;
		}

	public:
		Command()
		{
			this->status = 0;
		}
		void add(const char* val)
		{
			vec.push_back(val);
		}
		void add(const string& val)
		{
			vec.push_back(val);
		}
		template<class DATA_TYPE> void add(DATA_TYPE val)
		{
			add(to_string(val));
		}
		template<class DATA_TYPE, class ...ARGS> void add(DATA_TYPE val, ARGS ...args)
		{
			add(val);
			add(args...);
		}

	public:
		string toString() const
		{
			ostringstream out;

			out << "*" << vec.size() << "\r\n";

			for (const string& item : vec)
			{
				out << "$" << item.length() << "\r\n" << item << "\r\n";
			}

			return out.str();
		}
		string get(int idx) const
		{
			return res.at(idx);
		}
		const vector<string>& getDataList() const
		{
			return res;
		}
		int getResult(RedisConnect* redis, int timeout)
		{
			auto doWork = [&]() {
				string msg = toString();
				Socket& sock = redis->sock;

				if (sock.write(msg.c_str(), msg.length()) < 0) return NETERR;

				int len = 0;
				int delay = 0;
				int readed = 0;
				char* dest = redis->buffer;
				const int maxsz = redis->memsz;

				timeout *= 1000;

				while (readed < maxsz)
				{
					if ((len = sock.read(dest + readed, maxsz - readed, false)) < 0) return len;

					if (len == 0)
					{
						delay += SOCKET_TIMEOUT;

						if (delay > timeout) return TIMEOUT;
					}
					else
					{
						dest[readed += len] = 0;

						if ((len = parse(dest, readed)) == TIMEOUT)
						{
							delay = 0;
						}
						else
						{
							return len;
						}
					}
				}

				return PARAMERR;
			};

			status = 0;
			msg.clear();

			redis->code = doWork();

			if (redis->code < 0 && msg.empty())
			{
				switch (redis->code)
				{
				case SYSERR:
					msg = "system error";
					break;
				case NETERR:
					msg = "network error";
					break;
				case DATAERR:
					msg = "protocol error";
					break;
				case TIMEOUT:
					msg = "response timeout";
					break;
				case NOTFOUND:
					msg = "element not found";
					break;
				default:
					msg = "unknown error";
					break;
				}
			}

			redis->status = status;
			redis->msg = msg;

			return redis->code;
		}
	};

protected:
	int code = 0;
	int port = 0;
	int memsz = 0;
	int status = 0;
	int timeout = 0;
	char* buffer = NULL;

	string pwd;
	string msg;
	string host;
	Socket sock;

public:
	~RedisConnect()
	{
		close();
	}

public:
	int getStatus() const
	{
		return status;
	}
	int getErrorCode() const
	{
		return code;
	}
	string getErrorString() const
	{
		return msg;
	}

public:
	void close()
	{
		if (buffer)
		{
			delete[] buffer;
			buffer = NULL;
		}

		sock.close();
	}
	bool reconnect()
	{
		if (host.empty()) return false;

		return connect(host, port, timeout, memsz) && auth(pwd) > 0;
	}
	int execute(Command& cmd)
	{
		return cmd.getResult(this, timeout);
	}
	template<class DATA_TYPE, class ...ARGS>
	int execute(DATA_TYPE val, ARGS ...args)
	{
		Command cmd;

		cmd.add(val, args...);

		return cmd.getResult(this, timeout);
	}
	template<class DATA_TYPE, class ...ARGS>
	int execute(vector<string>& vec, DATA_TYPE val, ARGS ...args)
	{
		Command cmd;

		cmd.add(val, args...);

		cmd.getResult(this, timeout);

		if (code > 0) std::swap(vec, cmd.res);

		return code;
	}
	bool connect(const string& host, int port, int timeout = 3, int memsz = 1024 * 1024)
	{
		close();

		if (sock.connect(host, port, timeout))
		{
			sock.setSendTimeout(SOCKET_TIMEOUT);
			sock.setRecvTimeout(SOCKET_TIMEOUT);

			this->host = host;
			this->port = port;
			this->memsz = memsz;
			this->timeout = timeout;
			this->buffer = new char[memsz + 1];
		}

		return buffer ? true : false;
	}

public:
	int selectIndex(const int& index){
		return execute("select", index);
	}
	int ping()
	{
		return execute("ping");
	}
	int del(const string& key)
	{
		return execute("del", key);
	}
	int ttl(const string& key)
	{
		return execute("ttl", key) == OK ? status : code;
	}
	int hlen(const string& key)
	{
		return execute("hlen", key) == OK ? status : code;
	}
	int auth(const string& pwd)
	{
		this->pwd = pwd;

		if (pwd.empty()) return OK;

		return execute("auth", pwd);
	}
	int get(const string& key, string& val)
	{
		vector<string> vec;

		if (execute(vec, "get", key) <= 0) return code;

		val = vec[0];

		return code;
	}
	int decr(const string& key, int val = 1)
	{
		return execute("decrby", key, val);
	}
	int incr(const string& key, int val = 1)
	{
		return execute("incrby", key, val);
	}
	int expire(const string& key, int timeout)
	{
		return execute("expire", key, timeout);
	}
	int keys(vector<string>& vec, const string& key)
	{
		return execute(vec, "keys", key);
	}
	int hdel(const string& key, const string& filed)
	{
		return execute("hdel", key, filed);
	}
	int hget(const string& key, const string& filed, string& val)
	{
		vector<string> vec;

		if (execute(vec, "hget", key, filed) <= 0) return code;

		val = vec[0];

		return code;
	}
	int set(const string& key, const string& val, int timeout = 0)
	{
		return timeout > 0 ? execute("setex", key, timeout, val) : execute("set", key, val);
	}
	int hset(const string& key, const string& filed, const string& val)
	{
		return execute("hset", key, filed, val);
	}

public:
	int pop(const string& key, string& val)
	{
		return lpop(key, val);
	}
	int lpop(const string& key, string& val)
	{
		vector<string> vec;

		if (execute(vec, "lpop", key) <= 0) return code;

		val = vec[0];

		return code;
	}
	int rpop(const string& key, string& val)
	{
		vector<string> vec;

		if (execute(vec, "rpop", key) <= 0) return code;

		val = vec[0];

		return code;
	}
	int push(const string& key, const string& val)
	{
		return rpush(key, val);
	}
	int lpush(const string& key, const string& val)
	{
		return execute("lpush", key, val);
	}
	int rpush(const string& key, const string& val)
	{
		return execute("rpush", key, val);
	}
	int range(vector<string>& vec, const string& key, int start, int end)
	{
		return execute(vec, "lrange", key, start, end);
	}
	int lrange(vector<string>& vec, const string& key, int start, int end)
	{
		return execute(vec, "lrange", key, start, end);
	}

public:
	int zrem(const string& key, const string& filed)
	{
		return execute("zrem", key, filed);
	}
	int zadd(const string& key, const string& filed, int score)
	{
		return execute("zadd", key, score, filed);
	}
	int zrange(vector<string>& vec, const string& key, int start, int end, bool withscore = false)
	{
		return withscore ? execute(vec, "zrange", key, start, end, "withscores") : execute(vec, "zrange", key, start, end);
	}

protected:
	typedef map<shared_ptr<RedisConnect>, time_t> ConnectMap;

	static Mutex* GetMutex()
	{
		static Mutex mtx;
		return &mtx;
	}
	static RedisConnect* GetTemplate()
	{
		static RedisConnect redis;
		return &redis;
	}
	static ConnectMap* GetConnectMap()
	{
		static ConnectMap connmap;
		return &connmap;
	}

public:
	static bool CanUse()
	{
		static RedisConnect* temp = GetTemplate();
		return temp->port > 0;
	}
	static shared_ptr<RedisConnect> Instance()
	{
		static Mutex& mtx = *GetMutex();
		static RedisConnect& temp = *GetTemplate();
		static map<shared_ptr<RedisConnect>, time_t>& datmap = *GetConnectMap();

		time_t now = time(NULL);
		shared_ptr<RedisConnect> redis;

		if (now > 0)
		{
			Locker lk(mtx);

			for (auto item : datmap)
			{
				if (item.first.use_count() == 2)
				{
					redis = item.first;

					if (item.second + 60 > now) return redis;

					datmap.erase(item.first);

					break;
				}
			}
		}

		auto get = [&](){
			if (redis)
			{
				if (redis->ping() > 0 || redis->reconnect()) return true;
			}
			else
			{
				redis = make_shared<RedisConnect>();

				if (redis->connect(temp.host, temp.port, temp.timeout, temp.memsz))
				{
					if (redis->auth(temp.pwd)) return true;
				}
			}

			return false;
		};

		if (get())
		{
			Locker lk(mtx);

			if (datmap.size() < POOL_MAXLEN) datmap.insert(pair<shared_ptr<RedisConnect>, time_t>(redis, now));
		}

		return redis;
	}
	static void Setup(const string& host, int port, const string& pwd = "", int timeout = 3, int memsz = 1024 * 1024)
	{
#ifndef _MSC_VER
		signal(SIGPIPE, SIG_IGN);
#else
		WSADATA data; WSAStartup(MAKEWORD(2, 2), &data);
#endif
		RedisConnect* redis = GetTemplate();

		redis->pwd = pwd;
		redis->host = host;
		redis->port = port;
		redis->memsz = memsz;
		redis->timeout = timeout;
	}
};
///////////////////////////////////////////////////////////////
#endif