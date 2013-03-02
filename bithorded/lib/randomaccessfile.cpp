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


#include "randomaccessfile.hpp"

#include <boost/assert.hpp>
#include <boost/filesystem.hpp>
#include <fcntl.h>
#include <ios>
#include <sys/stat.h>
#include <sstream>

using namespace std;

namespace fs = boost::filesystem;

RandomAccessFile::RandomAccessFile() :
	_fd(-1), _path(""), _size(0)
{}

RandomAccessFile::RandomAccessFile(const boost::filesystem::path& path, RandomAccessFile::Mode mode, uint64_t size) :
	_fd(-1), _path(""), _size(0)
{
	open(path, mode, size);
}

RandomAccessFile::~RandomAccessFile()
{
	close();
}

void RandomAccessFile::open(const boost::filesystem::path& path, RandomAccessFile::Mode mode, uint64_t size)
{
	if (!size)
		size = fs::file_size(path);
	_path = path;
	_size = size;
	int m;
	switch (mode) {
		case READ: m = O_RDONLY; break;
		case WRITE: m = O_WRONLY|O_CREAT; break;
		case READWRITE: m = O_RDWR|O_CREAT; break;
		default: throw std::ios_base::failure("Unknown open-mode");
	}
	if (size > 0 && fs::exists(_path) && fs::file_size(_path) != size) {
		ostringstream buf;
		buf << path << " exists with mismatching size, (" << size << " : " << fs::file_size(_path) << ")";
		throw std::ios_base::failure(buf.str());
	}

	_fd = ::open(path.c_str(), m, S_IRUSR|S_IWUSR);
	if (_fd < 0)
		throw std::ios_base::failure("Failed opening "+path.string());
	else if (size > 0)
		ftruncate(_fd, size);
}

void RandomAccessFile::close()
{
	if (is_open()) {
		::close(_fd);
		_fd = -1;
		_path == "";
	}
}

bool RandomAccessFile::is_open() const
{
	return _fd != -1;
}

uint64_t RandomAccessFile::size() const
{
	return _size;
}

uint RandomAccessFile::blocks(size_t blockSize) const
{
	// Round up the number of blocks
	return (size() + blockSize - 1) / blockSize;
}

byte* RandomAccessFile::read(uint64_t offset, size_t& size, byte* buf) const
{
	ssize_t read = pread(_fd, buf, size, offset);
	if ( read > 0 ) {
		size = read;
		return buf;
	} else {
		size = 0;
		return NULL;
	}
}

ssize_t RandomAccessFile::write(uint64_t offset, const void* src, size_t size)
{
	ssize_t written = pwrite(_fd, src, size, offset);
	if ((size_t)written != size)
		throw std::ios_base::failure("Failed to write");
	return written;
}

const boost::filesystem::path& RandomAccessFile::path() const
{
	return _path;
}


