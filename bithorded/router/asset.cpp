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

#include <boost/smart_ptr/make_shared.hpp>
#include <utility>

#include <glog/logging.h>

using namespace bithorded::router;
using namespace std;

bool bithorded::router::ForwardedAsset::hasUpstream(const std::string peername)
{
	return _upstream.count(peername);
}

void bithorded::router::ForwardedAsset::bindUpstream(const bithorded::Client::Ptr& f, uint64_t uuid)
{
	if (f->requestsAsset(_ids)) // This path surely doesn't have the asset.
		return;
	auto upstream = new bithorde::ReadAsset(f, _ids);
	auto peername = f->peerName();
	upstream->statusUpdate.connect(boost::bind(&ForwardedAsset::onUpstreamStatus, this, peername, bithorde::ASSET_ARG_STATUS));
	upstream->dataArrived.connect(boost::bind(&ForwardedAsset::onData, this,
		bithorde::ASSET_ARG_OFFSET, bithorde::ASSET_ARG_DATA, bithorde::ASSET_ARG_TAG));
	auto& upstream_ = _upstream[peername];
	upstream_.reset(upstream);
	f->bind(*upstream_, uuid);
}

void bithorded::router::ForwardedAsset::onUpstreamStatus(const string& peername, const bithorde::AssetStatus& status)
{
	if (status.status() == bithorde::Status::SUCCESS) {
		DLOG(INFO) << "Found upstream " << peername;
		if (status.has_size()) {
			if (_size == -1) {
				_size = status.size();
			} else if (_size != (int64_t)status.size()) {
				LOG(WARNING) << peername << " responded with mismatching size, ignoring...";
				_upstream.erase(peername);
			}
		} else {
			LOG(WARNING) << peername << " SUCCESS response not accompanied with asset-size.";
		}
	} else {
		DLOG(INFO) << "Failed upstream " << peername;
		_upstream.erase(peername);
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

bool bithorded::router::ForwardedAsset::getIds(BitHordeIds& ids)
{
	ids = _ids;
	return true;
}

void bithorded::router::ForwardedAsset::async_read(uint64_t offset, size_t& size, ReadCallback cb)
{
	auto selector = _upstream.begin(); // TODO: Actually select the least loaded connection
	if (selector == _upstream.end())
		return cb(-1, string());
	PendingRead read;
	read.offset = offset;
	read.size = size;
	read.cb = cb;
	_pendingReads.push_back(read); // TODO: timeout
	selector->second->aSyncRead(offset, size);
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