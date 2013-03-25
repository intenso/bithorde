#ifndef LOOKUP_H
#define LOOKUP_H

#include <boost/shared_ptr.hpp>
#include <boost/signals2/connection.hpp>

#include <lib/bithorde.h>

#include "fuse++.hpp"

typedef std::pair<fuse_ino_t, std::string> LookupParams;

class BHFuse;
class FUSEAsset;

class Lookup
{
	BHFuse * fs;
	fuse_req_t req;
	boost::shared_ptr<FUSEAsset> fuseAsset; // Set if came from fuse_open()
	boost::shared_ptr<bithorde::ReadAsset> asset;
	boost::signals2::scoped_connection statusConnection;
public:
	explicit Lookup(BHFuse * fs, fuse_req_t req, const BitHordeIds& ids);
	explicit Lookup(BHFuse* fs, boost::shared_ptr< FUSEAsset >& asset, fuse_req_t req);

	void perform(bithorde::Client::Pointer& c);

private:
    void onStatusUpdate(const bithorde::AssetStatus & msg);
};

#endif // LOOKUP_H
