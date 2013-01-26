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


#ifndef BITHORDED_CACHE_MANAGER_HPP
#define BITHORDED_CACHE_MANAGER_HPP

#include <boost/asio/io_service.hpp>

#include "asset.hpp"
#include "../router/router.hpp"
#include "../store/assetstore.hpp"

namespace bithorded { namespace cache {

class CacheManager
{
	boost::filesystem::path _baseDir;
	boost::asio::io_service& _ioSvc;
	bithorded::router::Router& _router;
	bithorded::store::AssetStore _store;

	uintmax_t _maxSize;
public:
	CacheManager(boost::asio::io_service& ioSvc, bithorded::router::Router& router, const boost::filesystem::path& baseDir, intmax_t size);

	/**
	 * Finds an asset by bithorde HashId. (Only the tiger-hash is actually used)
	 */
	IAsset::Ptr findAsset(const BitHordeIds& ids);

	/**
	 * Add an asset to the idx, allocating space for
	 * the status of the asset will be updated to reflect it.
	 *
	 * If function returns true, /handler/ will be called on a thread running ioSvc.run()
	 *
	 * @returns a valid asset if file is within acceptable path, NULL otherwise
	 */
	IAsset::Ptr prepareUpload(uint64_t size);
private:
	bool makeRoom(uint64_t size);
	void linkAsset(bithorded::cache::CachedAsset* asset);
	/**
	 * Figures out which tiger-id hasn't been accessed recently.
	 */
	boost::filesystem::path pickLooser();
};
} }

#endif // BITHORDED_CACHE_STORE_HPP
