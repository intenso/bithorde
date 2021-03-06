/*
    Copyright 2012 <copyright holder> <email>

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


#include "asset.hpp"

#include <boost/foreach.hpp>
#include <boost/smart_ptr/make_shared.hpp>
#include <utility>

#include <lib/weak_fn.hpp>

#include <log4cplus/logger.h>
#include <log4cplus/loggingmacros.h>

using namespace bithorded::router;
using namespace std;

namespace bithorded { namespace router {
	log4cplus::Logger assetLogger = log4cplus::Logger::getInstance("router");
} }

UpstreamAsset::UpstreamAsset(const bithorde::ReadAsset::ClientPointer& client, const BitHordeIds& requestIds)
	: ReadAsset(client, requestIds)
{}

void UpstreamAsset::handleMessage(const bithorde::Read::Response& resp)
{
	auto self_ref = shared_from_this();
	bithorde::ReadAsset::handleMessage(resp);
}

bool bithorded::router::ForwardedAsset::hasUpstream(const std::string peername)
{
	return _upstream.count(peername);
}

void bithorded::router::ForwardedAsset::bindUpstreams(const std::map< string, bithorded::Client::Ptr >& friends, uint64_t uuid, int timeout)
{
	BOOST_FOREACH(auto f_, friends) {
		auto f = f_.second;
		if (f->requestsAsset(_ids)) // This path surely doesn't have the asset.
			continue;
		auto peername = f->peerName();
		auto& upstream = _upstream[peername] = make_shared<UpstreamAsset>(f, _ids);
		auto self = boost::weak_ptr<ForwardedAsset>(shared_from_this());
		upstream->statusUpdate.connect(boost::bind(boost::weak_fn(&ForwardedAsset::onUpstreamStatus, self), peername, bithorde::ASSET_ARG_STATUS));
		upstream->dataArrived.connect(boost::bind(boost::weak_fn(&ForwardedAsset::onData, self),
			bithorde::ASSET_ARG_OFFSET, bithorde::ASSET_ARG_DATA, bithorde::ASSET_ARG_TAG));
		if (!f->bind(*upstream, timeout, uuid))
			dropUpstream(peername);
	}
	updateStatus();
}

void bithorded::router::ForwardedAsset::onUpstreamStatus(const string& peername, const bithorde::AssetStatus& status)
{
	if (status.status() == bithorde::Status::SUCCESS) {
		LOG4CPLUS_DEBUG(assetLogger, _ids << " Found upstream " << peername);
		if (status.has_size()) {
			if (_size == -1) {
				_size = status.size();
			} else if (_size != (int64_t)status.size()) {
				LOG4CPLUS_WARN(assetLogger, peername << " " << _ids << " responded with mismatching size, ignoring...");
				dropUpstream(peername);
			}
		} else {
			LOG4CPLUS_WARN(assetLogger, peername << " " << _ids << " SUCCESS response not accompanied with asset-size.");
		}
	} else {
		LOG4CPLUS_DEBUG(assetLogger, _ids << "Failed upstream " << peername);
		dropUpstream(peername);
	}
	updateStatus();
}

void bithorded::router::ForwardedAsset::updateStatus() {
	bithorde::Status status = _upstream.empty() ? bithorde::Status::NOTFOUND : bithorde::Status::NONE;
	for (auto iter=_upstream.begin(); iter!=_upstream.end(); iter++) {
		auto& asset = iter->second;
		if (asset->status == bithorde::Status::SUCCESS)
			status = bithorde::Status::SUCCESS;
	}
	setStatus(status);
}

size_t bithorded::router::ForwardedAsset::can_read(uint64_t offset, size_t size)
{
	return size;
}

bool bithorded::router::ForwardedAsset::getIds(BitHordeIds& ids) const
{
	ids = _ids;
	return true;
}

void bithorded::router::ForwardedAsset::async_read(uint64_t offset, size_t& size, uint32_t timeout, ReadCallback cb)
{
	if (_upstream.empty())
		return cb(-1, string());
	auto chosen = _upstream.begin();
	uint32_t current_best = 1000*60*60*24;
	for (auto iter = _upstream.begin(); iter != _upstream.end(); iter++) {
		auto& a = iter->second;
		if (a->status != bithorde::SUCCESS)
			continue;
		if (current_best > a->readResponseTime.value()) {
			current_best = a->readResponseTime.value();
			chosen = iter;
		}
	}
	PendingRead read;
	read.offset = offset;
	read.size = size;
	read.cb = cb;
	_pendingReads.push_back(read);
	chosen->second->aSyncRead(offset, size, timeout);
}

void bithorded::router::ForwardedAsset::onData(uint64_t offset, const std::string& data, int tag) {
	for (auto iter=_pendingReads.begin(); iter != _pendingReads.end(); ) {
		if (iter->offset == offset) {
			iter->cb(offset, data);
			iter = _pendingReads.erase(iter); // Will increase the iterator
		} else {
			iter++;	// Move to next
		}
	}
}

uint64_t bithorded::router::ForwardedAsset::size()
{
	return _size;
}

void ForwardedAsset::inspect(bithorded::management::InfoList& target) const
{
	target.append("type") << "forwarded";
}

void ForwardedAsset::inspect_upstreams(bithorded::management::InfoList& target) const
{
	for (auto iter = _upstream.begin(); iter != _upstream.end(); iter++) {
		ostringstream buf;
		buf << "upstream_" << iter->first;
		target.append(buf.str()) << bithorde::Status_Name(iter->second->status) << ", responseTime: " << iter->second->readResponseTime;
	}
}


void ForwardedAsset::dropUpstream(const string& peername)
{
	auto upstream = _upstream.find(peername);
	if (upstream != _upstream.end()) {
		upstream->second->statusUpdate.disconnect(boost::bind(&ForwardedAsset::onUpstreamStatus, this, peername, bithorde::ASSET_ARG_STATUS));
		upstream->second->dataArrived.disconnect(boost::bind(&ForwardedAsset::onData, this,
			bithorde::ASSET_ARG_OFFSET, bithorde::ASSET_ARG_DATA, bithorde::ASSET_ARG_TAG));
		_upstream.erase(upstream);
	}
}
