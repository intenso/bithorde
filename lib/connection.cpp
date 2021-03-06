#include "connection.h"

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <iostream>

#define CRYPTOPP_ENABLE_NAMESPACE_WEAK 1

#include <crypto++/arc4.h>
#include <crypto++/aes.h>
#include <crypto++/hmac.h>
#include <crypto++/modes.h>
#include <crypto++/sha.h>

#include <google/protobuf/wire_format_lite.h>
#include <google/protobuf/wire_format_lite_inl.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>

const size_t K = 1024;
const size_t M = 1024*K;
const size_t MAX_MSG = 130*K;
const size_t SEND_BUF = 1024*K;
const size_t SEND_BUF_EMERGENCY = SEND_BUF + 64*K;
const size_t SEND_BUF_LOW_WATER_MARK = SEND_BUF/4;
const size_t SEND_CHUNK_MS = 50;

namespace asio = boost::asio;
namespace chrono = boost::chrono;
using namespace std;

using namespace bithorde;

typedef CryptoPP::HMAC<CryptoPP::SHA256> HMAC_SHA256;

template <typename Protocol>
class ConnectionImpl : public Connection {
	typedef typename Protocol::socket Socket;
	typedef typename Protocol::endpoint EndPoint;

	boost::shared_ptr<Socket> _socket;

	boost::shared_ptr<CryptoPP::SymmetricCipher> _encryptor, _decryptor;
public:
	ConnectionImpl(boost::asio::io_service& ioSvc, const ConnectionStats::Ptr& stats, const EndPoint& addr)
		: Connection(ioSvc, stats), _socket(new Socket(ioSvc))
	{
		_socket->connect(addr);
	}

	ConnectionImpl(boost::asio::io_service& ioSvc, const ConnectionStats::Ptr& stats, boost::shared_ptr<Socket>& socket)
		: Connection(ioSvc, stats)
	{
		_socket = socket;
	}

	~ConnectionImpl() {
		close();
	}

	virtual void setEncryption(bithorde::CipherType t, const std::string& key, const std::string& iv) {
		switch (t) {
			case bithorde::CipherType::CLEARTEXT:
				_encryptor.reset();
				break;
			case bithorde::CipherType::AES_CTR:
				_encryptor.reset(new CryptoPP::CTR_Mode<CryptoPP::AES>::Encryption());
				_encryptor->SetKeyWithIV((const byte*)key.data(), key.size(), (const byte*)iv.data(), iv.size());
				break;
			case bithorde::CipherType::RC4:
				byte key_[HMAC_SHA256::DIGESTSIZE];
				HMAC_SHA256((const byte*)key.data(), key.size()).CalculateDigest(key_, (const byte*)iv.data(), iv.size());
				_encryptor.reset(new CryptoPP::Weak1::ARC4::Encryption());
				_encryptor->SetKey(key_, sizeof(key_));
				break;
			case bithorde::CipherType::XOR:
			default:
				throw std::runtime_error("Unsupported Cipher " + bithorde::CipherType_Name(t));
		}
	}

	virtual void setDecryption(bithorde::CipherType t, const std::string& key, const std::string& iv) {
		switch (t) {
			case bithorde::CipherType::CLEARTEXT:
				_decryptor.reset();
				break;
			case bithorde::CipherType::AES_CTR:
				_decryptor.reset(new CryptoPP::CTR_Mode<CryptoPP::AES>::Decryption());
				_decryptor->SetKeyWithIV((const byte*)key.data(), key.size(), (const byte*)iv.data(), iv.size());
				break;
			case bithorde::CipherType::RC4:
				byte key_[HMAC_SHA256::DIGESTSIZE];
				HMAC_SHA256((const byte*)key.data(), key.size()).CalculateDigest(key_, (const byte*)iv.data(), iv.size());
				_decryptor.reset(new CryptoPP::Weak1::ARC4::Decryption());
				_decryptor->SetKey(key_, sizeof(key_));
				break;
			case bithorde::CipherType::XOR:
			default:
				throw std::runtime_error("Unsupported Cipher " + bithorde::CipherType_Name(t));
		}
	}

	void trySend() {
		_sendWaiting = 0;
		auto queued = _sndQueue.dequeue(_stats->outgoingBitrateCurrent.value()/8, SEND_CHUNK_MS);
		std::vector<boost::asio::const_buffer> buffers;
		buffers.reserve(queued.size());
		for (auto iter=queued.begin(); iter != queued.end(); iter++) {
			auto& buf = (*iter)->buf;
			if (_encryptor)
				_encryptor->ProcessString((byte*)buf.data(), buf.size());
			buffers.push_back(boost::asio::buffer(buf));
			_sendWaiting += buf.size();
		}
		if (_sendWaiting) {
			boost::asio::async_write(*_socket, buffers,
				boost::bind(&Connection::onWritten, shared_from_this(),
							asio::placeholders::error, asio::placeholders::bytes_transferred, queued)
			);
		}
	}

	void tryRead() {
		_socket->async_read_some(asio::buffer(_rcvBuf.allocate(MAX_MSG), MAX_MSG),
			boost::bind(&Connection::onRead, shared_from_this(),
				asio::placeholders::error, asio::placeholders::bytes_transferred
			)
		);
	}

	virtual void decrypt(byte* buf, size_t size) {
		if (_decryptor)
			_decryptor->ProcessString(buf, size);
	}

	void close() {
		if (_socket->is_open()) {
			_socket->close();
			disconnected();
		}
	}
};

Message::Deadline Message::NEVER(Message::Deadline::max());
Message::Deadline Message::in(int msec)
{
	return Clock::now()+chrono::milliseconds(msec);
}

Message::Message(Deadline expires) :
	expires(expires)
{
}

MessageQueue::MessageQueue()
	: _size(0)
{}

std::string MessageQueue::_empty;

bool MessageQueue::empty() const
{
	return _queue.empty();
}

void MessageQueue::enqueue(Message* msg)
{
	_queue.push_back(msg);
	_size += msg->buf.size();
}

std::vector< const Message* > MessageQueue::dequeue(size_t bytes_per_sec, ushort millis)
{
	bytes_per_sec = std::max(bytes_per_sec, 1*K);
	std::size_t bytes(((bytes_per_sec*millis)/1000)+1), target_size((_size <= bytes) ? 0 : _size - bytes);
	auto now = chrono::steady_clock::now();
	std::vector<const Message*> res;
	res.reserve(_size);
	while ((_size > target_size) && !_queue.empty()) {
		Message* next = _queue.front();
		_queue.pop_front();
		_size -= next->buf.size();
		if (now < next->expires)
			res.push_back(next);
	}
	return res;
}

size_t MessageQueue::size() const
{
	return _size;
}

ConnectionStats::ConnectionStats(const TimerService::Ptr& ts) :
	_ts(ts),
	incomingMessagesCurrent(*_ts, "msgs/s", boost::posix_time::seconds(1), 0.2),
	incomingBitrateCurrent(*_ts, "bit/s", boost::posix_time::seconds(1), 0.2),
	outgoingMessagesCurrent(*_ts, "msgs/s", boost::posix_time::seconds(1), 0.2),
	outgoingBitrateCurrent(*_ts, "bit/s", boost::posix_time::seconds(1), 0.2),
	incomingMessages("msgs"),
	incomingBytes("bytes"),
	outgoingMessages("msgs"),
	outgoingBytes("bytes")
{
}

Connection::Connection(asio::io_service & ioSvc, const ConnectionStats::Ptr& stats) :
	_ioSvc(ioSvc),
	_stats(stats),
	_sendWaiting(0)
{
}

Connection::Pointer Connection::create(asio::io_service& ioSvc, const ConnectionStats::Ptr& stats, const asio::ip::tcp::endpoint& addr)  {
	Pointer c(new ConnectionImpl<asio::ip::tcp>(ioSvc, stats, addr));
	c->tryRead();
	return c;
}

Connection::Pointer Connection::create(asio::io_service& ioSvc, const ConnectionStats::Ptr& stats, boost::shared_ptr< asio::ip::tcp::socket >& socket)
{
	Pointer c(new ConnectionImpl<asio::ip::tcp>(ioSvc, stats, socket));
	c->tryRead();
	return c;
}

Connection::Pointer Connection::create(asio::io_service& ioSvc, const ConnectionStats::Ptr& stats, const asio::local::stream_protocol::endpoint& addr)  {
	Pointer c(new ConnectionImpl<asio::local::stream_protocol>(ioSvc, stats, addr));
	c->tryRead();
	return c;
}

Connection::Pointer Connection::create(asio::io_service& ioSvc, const ConnectionStats::Ptr& stats, boost::shared_ptr< asio::local::stream_protocol::socket >& socket)
{
	Pointer c(new ConnectionImpl<asio::local::stream_protocol>(ioSvc, stats, socket));
	c->tryRead();
	return c;
}

void Connection::onRead(const boost::system::error_code& err, size_t count)
{
	if (err || (count == 0)) {
		close();
		return;
	} else {
		decrypt(_rcvBuf.ptr+_rcvBuf.size, count);
		_rcvBuf.charge(count);
		_stats->incomingBitrateCurrent += count*8;
		_stats->incomingBytes += count;
	}

	google::protobuf::io::CodedInputStream stream((::google::protobuf::uint8*)_rcvBuf.ptr, _rcvBuf.size);
	bool res = true;
	size_t remains;
	while (res) {
		remains = stream.BytesUntilLimit();
		uint32_t tag = stream.ReadTag();
		if (tag == 0)
			break;
		switch (::google::protobuf::internal::WireFormatLite::GetTagFieldNumber(tag)) {
		case HandShake:
			res = dequeue<bithorde::HandShake>(HandShake, stream); break;
		case BindRead:
			res = dequeue<bithorde::BindRead>(BindRead, stream); break;
		case AssetStatus:
			res = dequeue<bithorde::AssetStatus>(AssetStatus, stream); break;
		case ReadRequest:
			res = dequeue<bithorde::Read::Request>(ReadRequest, stream); break;
		case ReadResponse:
			res = dequeue<bithorde::Read::Response>(ReadResponse, stream); break;
		case BindWrite:
			res = dequeue<bithorde::BindWrite>(BindWrite, stream); break;
		case DataSegment:
			res = dequeue<bithorde::DataSegment>(DataSegment, stream); break;
		case HandShakeConfirmed:
			res = dequeue<bithorde::HandShakeConfirmed>(HandShakeConfirmed, stream); break;
		case Ping:
			res = dequeue<bithorde::Ping>(Ping, stream); break;
		default:
			cerr << "BitHorde protocol warning: unknown message tag" << endl;
			res = ::google::protobuf::internal::WireFormatLite::SkipMessage(&stream);
		}
	}

	_rcvBuf.pop(_rcvBuf.size-remains);

	tryRead();
	return;
}

template <class T>
bool Connection::dequeue(MessageType type, ::google::protobuf::io::CodedInputStream &stream) {
	bool res;
	T msg;

	uint32_t length;
	if (!stream.ReadVarint32(&length)) return false;

	uint32_t bytesLeft = stream.BytesUntilLimit();
	if (length > bytesLeft) return false;

	_stats->incomingMessages += 1;
	_stats->incomingMessagesCurrent += 1;
	::google::protobuf::io::CodedInputStream::Limit limit = stream.PushLimit(length);
	if ((res = msg.MergePartialFromCodedStream(&stream))) {
		message(type, msg);
	}
	stream.PopLimit(limit);

	return res;
}

ConnectionStats::Ptr Connection::stats()
{
	return _stats;
}

bool Connection::sendMessage(Connection::MessageType type, const google::protobuf::Message& msg, const Message::Deadline& expires, bool prioritized)
{
	size_t bufLimit = prioritized ? SEND_BUF_EMERGENCY : SEND_BUF;
	if (_sndQueue.size() > bufLimit)
		return false;

	auto buf = new Message(expires);
	// Encode
	{
		::google::protobuf::io::StringOutputStream of(&buf->buf);
		::google::protobuf::io::CodedOutputStream stream(&of);
		stream.WriteTag(::google::protobuf::internal::WireFormatLite::MakeTag(type, ::google::protobuf::internal::WireFormatLite::WIRETYPE_LENGTH_DELIMITED));
		stream.WriteVarint32(msg.ByteSize());
		bool encoded = msg.SerializeToCodedStream(&stream);
		BOOST_VERIFY(encoded);
	}
	_sndQueue.enqueue(buf);

	_stats->outgoingMessages += 1;
	_stats->outgoingMessagesCurrent += 1;

	// Push out at once unless _queued;
	if (_sendWaiting == 0)
		trySend();
	return true;
}

void Connection::onWritten(const boost::system::error_code& err, size_t written, std::vector<const Message*> queued) {
	size_t queued_bytes(0);
	for (auto iter=queued.begin(); iter != queued.end(); iter++) {
		queued_bytes += (*iter)->buf.size();
		delete *iter;
	}
	if ((!err) && (written == queued_bytes) && (written>0)) {
		_stats->outgoingBitrateCurrent += written*8;
		_stats->outgoingBytes += written;
		trySend();
		if (_sndQueue.size() < SEND_BUF_LOW_WATER_MARK)
			writable();
	} else {
		cerr << "Failed to write. Disconnecting..." << endl;
		close();
	}
}
