#include "main.h"
#include "inode.h"
#include "lookup.h"

#include <errno.h>
#include <boost/make_shared.hpp>

using namespace std;

using namespace bithorde;

Lookup::Lookup(BHFuse* fs, fuse_req_t req, const BitHordeIds& ids) :
	fs(fs),
	req(req)
{
	asset = boost::make_shared<ReadAsset>(fs->client, ids);
}

Lookup::Lookup( BHFuse* fs, boost::shared_ptr< FUSEAsset >& asset, fuse_req_t req) :
	fs(fs),
	req(req),
	fuseAsset(asset),
	asset(asset->asset)
{}

void Lookup::perform(Client::Pointer& c)
{
	statusConnection = asset->statusUpdate.connect(Asset::StatusSignal::slot_type(&Lookup::onStatusUpdate, this, _1));
	c->bind(*asset);
}

void Lookup::onStatusUpdate(const bithorde::AssetStatus &msg)
{
	if (msg.status() == ::bithorde::SUCCESS) {
		if (fuseAsset) {
			fuseAsset->fuse_reply_open(req);
		} else {
			FUSEAsset* f_asset = fs->registerAsset(asset);
			f_asset->fuse_reply_lookup(req);
		}
	} else {
		fuse_reply_err(req, ENOENT);
	}
	delete this;
}


