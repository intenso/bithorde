/*
    Copyright 2013 <copyright holder> <email>

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


#include "assetsessions.hpp"

#include "../../lib/hashes.h"

bithorded::IAsset::Ptr bithorded::AssetSessions::findAsset(const bithorde::BindRead& req)
{
	std::string tigerId = findBithordeId(req.ids(), bithorde::HashType::TREE_TIGER);
	if (tigerId.empty())
		return IAsset::Ptr();
	if (auto active = _tigerCache[tigerId])
		return active;

	auto res = openAsset(req);
	add(tigerId, res);
	return res;
}

void bithorded::AssetSessions::add(const std::string& tigerId, const bithorded::IAsset::Ptr& asset)
{
	if (asset)
		_tigerCache.set(tigerId, asset);
}


