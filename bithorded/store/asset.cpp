/*
    Copyright 2012 Ulrik Mikaelsson <email>

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
#include "../lib/rounding.hpp"

const size_t MAX_CHUNK = 64*1024;

using namespace std;
using namespace bithorded::store;

StoredAsset::StoredAsset(const boost::filesystem::path& metaFolder, RandomAccessFile::Mode mode) :
	_metaFolder(metaFolder),
	_file(metaFolder/"data", mode),
	_metaStore(metaFolder/"meta", _file.blocks(BLOCKSIZE)),
	_hasher(_metaStore)
{

}

StoredAsset::StoredAsset(const boost::filesystem::path& metaFolder, RandomAccessFile::Mode mode, uint64_t size) :
	_metaFolder(metaFolder),
	_file(metaFolder/"data", mode, size),
	_metaStore(metaFolder/"meta", _file.blocks(BLOCKSIZE)),
	_hasher(_metaStore)
{

}

void StoredAsset::async_read(uint64_t offset, size_t& size, uint32_t timeout, bithorded::IAsset::ReadCallback cb)
{
	byte buf[MAX_CHUNK];
	const byte* data = _file.read(offset, size, buf);
	if (data)
		cb(offset, std::string((char*)data, size));
	else
		cb(offset, std::string());
}

size_t StoredAsset::can_read(uint64_t offset, size_t size)
{
	BOOST_ASSERT(size > 0);
	size_t res = 0;
	if (size > MAX_CHUNK)
		size = MAX_CHUNK;
	auto stopoffset = offset+size;
	auto lastbyteoffset = stopoffset-1;
	uint32_t firstBlock = offset / BLOCKSIZE;
	uint32_t lastBlock = lastbyteoffset / BLOCKSIZE;

	for (auto currentBlock = firstBlock; currentBlock <= lastBlock && _hasher.isBlockSet(currentBlock); currentBlock++) {
		res += BLOCKSIZE;
		if (currentBlock == firstBlock)
			res -= offset % BLOCKSIZE;
		if (currentBlock == lastBlock) {
			if (auto overflow = (stopoffset % BLOCKSIZE))
				res -= BLOCKSIZE - overflow;
		}
	}

	return res;
}

bool StoredAsset::getIds(BitHordeIds& ids) const
{
	BOOST_ASSERT( ids.size() == 0 );
	auto root = _hasher.getRoot();

	if (root->state == TigerNode::State::SET) {
		auto tigerId = ids.Add();
		tigerId->set_type(bithorde::TREE_TIGER);
		tigerId->set_id(root->digest, TigerNode::DigestSize);
		return true;
	} else {
		return false;
	}
}

bool StoredAsset::hasRootHash()
{
	auto root = _hasher.getRoot();
	return (root->state == TigerNode::State::SET);
}

void StoredAsset::notifyValidRange(uint64_t offset, uint64_t size)
{
	uint64_t filesize = StoredAsset::size();
	uint64_t end = offset + size;
	offset = roundUp(offset, BLOCKSIZE);
	if (end != filesize)
		end = roundDown(end, BLOCKSIZE);

	updateHash(offset, end);
}

uint64_t StoredAsset::size() {
	return _file.size();
}

boost::filesystem::path StoredAsset::folder()
{
	return _metaFolder;
}

void StoredAsset::updateStatus()
{
	if (hasRootHash())
		setStatus(bithorde::SUCCESS);
}

void StoredAsset::updateHash(uint64_t offset, uint64_t end)
{
	byte BUF[BLOCKSIZE];

	while (offset < end) {
		size_t blockSize = BLOCKSIZE;
		if (offset+blockSize > end)
			blockSize = end - offset;
		size_t read = blockSize;

		byte* buf = _file.read(offset, read, BUF);
		if (read != blockSize)
			throw ios_base::failure("Unexpected read error");

		_hasher.setData(offset/BLOCKSIZE, buf, read);

		offset += blockSize;
	}
}

