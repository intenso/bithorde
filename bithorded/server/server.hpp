/*
    Copyright 2012 Ulrik Mikaelsson <ulrik.mikaelsson@gmail.com>

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/


#ifndef BITHORDED_SERVER_H
#define BITHORDED_SERVER_H

#include <list>
#include <memory>
#include <vector>

#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/smart_ptr/scoped_ptr.hpp>

#include "../cache/manager.hpp"
#include "../http_server/server.hpp"
#include "../lib/loopfilter.hpp"
#include "../lib/management.hpp"
#include "../router/router.hpp"
#include "../source/store.hpp"
#include "bithorde.pb.h"
#include "client.hpp"

namespace bithorded {

class Config;

class BindError : public std::runtime_error {
public:
	bithorde::Status status;
	explicit BindError(bithorde::Status status);
};

class ConnectionList : public WeakMap<std::string, bithorded::Client>, public management::DescriptiveDirectory {
	virtual void inspect(management::InfoList& target) const;
	virtual void describe(management::Info& target) const;
};

class Server : public management::Directory
{
	Config &_cfg;
	boost::asio::io_service& _ioSvc;
	TimerService::Ptr _timerSvc;

	boost::asio::ip::tcp::acceptor _tcpListener;
	boost::asio::local::stream_protocol::acceptor _localListener;

	LoopFilter _loopFilter;
	ConnectionList _connections;

	std::vector< std::unique_ptr<bithorded::source::Store> > _assetStores;
	router::Router _router;
	cache::CacheManager _cache;
	std::unique_ptr<http::server::server> _httpInterface;
public:
	Server(boost::asio::io_service& ioSvc, Config& cfg);

	boost::asio::io_service& ioService();
	std::string name() { return _cfg.nodeName; }
	const Config::Client& getClientConfig(const std::string& name);

	IAsset::Ptr async_linkAsset(const boost::filesystem::path& filePath);
	IAsset::Ptr async_findAsset(const bithorde::BindRead& req);
    IAsset::Ptr prepareUpload(uint64_t size);

	void hookup(boost::shared_ptr< boost::asio::ip::tcp::socket >& socket, const Config::Client& client);

	virtual void inspect(management::InfoList& target) const;
private:
	void clientConnected(const bithorded::Client::Ptr& client);
	void clientAuthenticated(const bithorded::Client::WeakPtr& client);
	void clientDisconnected(bithorded::Client::Ptr& client);

	void waitForTCPConnection();
	void waitForLocalConnection();
	void onTCPConnected(boost::shared_ptr< boost::asio::ip::tcp::socket >& socket, const boost::system::error_code& ec);
	void onLocalConnected(boost::shared_ptr< boost::asio::local::stream_protocol::socket >& socket, const boost::system::error_code& ec);
};

}
#endif // BITHORDED_SERVER_H
