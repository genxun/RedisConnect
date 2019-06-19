# RedisConnect介绍
#### 1、RedisConnect是基于C++11实现的简单易用的Redis客户端。
#### 2、源码只包含一个头文件与一个命令行工具源文件，无需编译安装，真正做到零依赖。
#### 3、自带连接池功能，调用Setup方法初始化连接池，然后执行Instance方法就可以获取一个连接。
#### 4、RedisConnect包装了常用的redis命令，对于未包装的命令你可以使用可变参模板方法(execute)进行调用。

# 安装方法
#### 1、下载源码
##### git clone https://gitee.com/xungen/redisconnect.git

#### 2、直接在工程中包含<a href='https://gitee.com/xungen/redisconnect/blob/master/RedisConnect.h' target='_blank'>RedisConnect.h</a>头文件即可(示例代码如下)
```
#include "RedisConnect.h"

int main(int argc, char** argv)
{
	string val;
 
	//初始化连接池
	RedisConnect::Setup("127.0.0.1", 6379, "password");
 
	//从连接池中获取一个连接
	shared_ptr<RedisConnect> redis = RedisConnect::Instance();
 
	//设置一个键值
	redis->set("key", "val");
	
	//获取键值内容
	redis->get("key", val);
 
	//执行expire命令设置超时时间
	redis->execute("expire", "key", 60);
 
	//获取超时时间(与ttl(key)方法等价)
	redis->execute("ttl", "key");
 
	//调用getStatus方法获取ttl命令执行结果
	printf("超时时间：%d\n", redis->getStatus());
 
	//执行del命令删除键值
	redis->execute("del", "key");
 
	return 0;
}
```
#### 3、RedisConnect自带一个命令行客户端工具
##### 直接在源码目录执行make命令就可完成客户端工具的编译，工具名称为redis，使用工具前你需要设置以下环境变量，然后将redis程序复制到系统/usr/bin目录下
```
# redis服务地址与端口
export REDIS_HOST=127.0.0.1:6379
 
# redis连接的认证密码(为空说明无需认证)
export REDIS_PASSWORD=password
```
##### 设置好上面的环境变量你可以使用redis客户端，使用方法如下
```
# 设置一个键值
redis set key val
 
# 获取指定键值
redis get key
 
# 设置有效时间
redis expire key 60
 
# 获取有效时间
redis ttl key
```