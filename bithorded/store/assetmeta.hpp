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


#ifndef BITHORDED_ASSETMETA_H
#define BITHORDED_ASSETMETA_H

#include <boost/filesystem/path.hpp>
#include <boost/iostreams/device/mapped_file.hpp>

#include <crypto++/tiger.h>

#include "bithorded/lib/hashtree.hpp"
#include "lib/types.h"

namespace bithorded { namespace store {

typedef HashNode<CryptoPP::Tiger> TigerNode;

class AssetMeta
{
public:
	typedef typename bithorded::store::TigerNode* NodePtr;
	AssetMeta(const boost::filesystem::path& path, uint leafBlocks);

	NodePtr operator[](const std::size_t offset);
	size_t size();

	const boost::filesystem::path& path() const;
private:
	void repage(uint64_t offset);

	boost::filesystem::path _path;

	boost::iostreams::mapped_file_params _fp;
	boost::iostreams::mapped_file _f;

	size_t _leafBlocks;
	size_t _nodes_offset;
	uint64_t _file_size;
	size_t _slice_size;
};

} }
#endif // BITHORDED_ASSETMETA_H
