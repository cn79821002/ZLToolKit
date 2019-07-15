﻿/*
 * MIT License
 *
 * Copyright (c) 2016-2019 xiongziliang <771730766@qq.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <signal.h>
#include <iostream>
#include "Util/CMD.h"
#include "Util/logger.h"
#include "Util/util.h"
#include "Network/TcpSession.h"
#include "Network/TcpServer.h"

using namespace std;
using namespace toolkit;

/**
* 回显会话
*/
class EchoSession : public TcpSession {
public:
    EchoSession(const Socket::Ptr &pSock) : TcpSession(pSock){
        DebugL;
    }
    virtual ~EchoSession(){
        DebugL;
    }

    void onRecv(const Buffer::Ptr &buffer) override {
        send(buffer);
    }
    void onError(const SockException &err) override{
        WarnL << err.what();
    }

	void onManager() override {}
};

//命令(http)
class CMD_pingpong: public CMD {
public:
    CMD_pingpong(){
		_parser.reset(new OptionParser(nullptr));
		(*_parser) << Option('l', "listen",   Option::ArgRequired, "10000",                       false, "服务器模式：监听端口",                nullptr);
		//测试客户端个数，默认10个
		(*_parser) << Option('c', "count",    Option::ArgRequired, to_string(10).data(),          false, "客户端模式：测试数据块大小",           nullptr);
		//默认每次发送1MB的数据
		(*_parser) << Option('b', "block",    Option::ArgRequired, to_string(1024 * 1024).data(), false, "客户端模式：测试数据块大小",           nullptr);
		//默认1秒发送10次，总速度率为1MB/s * 10 * 10 = 100MB/s
		(*_parser) << Option('i', "interval", Option::ArgRequired, to_string(100).data(),         false, "客户端模式：测试数据发送间隔，单位毫秒", nullptr);
		//指定服务器地址
		(*_parser) << Option('s', "server",   Option::ArgRequired, "127.0.0.1:10000",             false, "客户端模式：测试服务器地址", []
				(const std::shared_ptr<ostream> &stream, const string &arg) {
			if (arg.find(":") == string::npos) {
				//中断后续选项的解析以及解析完毕回调等操作
				throw std::runtime_error("\t地址必须指明端口号.");
			}
			//如果返回false则忽略后续选项的解析
			return true;
		});
	}

	~CMD_pingpong() {}

	const char *description() const override {
		return "tcp回显性能测试";
	}
};


EventPoller::Ptr nextPoller(){
    static vector<EventPoller::Ptr> s_poller_vec;
    static int  s_poller_index = 0;
    if(s_poller_vec.empty()){
        EventPollerPool::Instance().for_each([&](TaskExecutor::Ptr &executor){
            s_poller_vec.emplace_back(dynamic_pointer_cast<EventPoller>(executor));
        });
    }
    auto ret = s_poller_vec[s_poller_index++];
    if(s_poller_index == s_poller_vec.size()){
        s_poller_index = 0;
    }
    return ret;
}

int main(int argc,char *argv[]){
	CMD_pingpong cmd;
	try{
		cmd(argc,argv);
	}catch (std::exception &ex){
		cout << ex.what() << endl;
		return 0;
	}

    //初始化环境
	Logger::Instance().add(std::shared_ptr<ConsoleChannel>(new ConsoleChannel()));
	Logger::Instance().setWriter(std::shared_ptr<LogWriter>(new AsyncLogWriter()));

	{
		int interval = cmd["interval"];
		int block 	 = cmd["block"];
		auto ip      = cmd.splitedVal("server")[0];
		int port     = cmd.splitedVal("server")[1];
        BufferRaw::Ptr buffer = std::make_shared<BufferRaw>(block);
        buffer->setSize(block);

		TcpServer::Ptr server(new TcpServer);
		server->start<EchoSession>(cmd["listen"]);
		for(auto i = 0; i < cmd["count"].as<int>() ; ++i){
			auto poller = nextPoller();
			auto socket = std::make_shared<Socket>(poller, false);

            socket->connect(ip,port,[socket,poller,interval,buffer](const SockException &err){
				if(err){
					WarnL << err.what();
					return;
				}
				socket->setOnRead([](const Buffer::Ptr &buf, struct sockaddr *addr , int addr_len){});
				poller->doDelayTask(interval,[socket,interval,buffer](){
					socket->send(buffer);
					return interval;
				});
			});

		}

		//设置退出信号处理函数
		static semaphore sem;
		signal(SIGINT, [](int) { sem.post(); });// 设置退出信号
		sem.wait();
	}
	return 0;
}