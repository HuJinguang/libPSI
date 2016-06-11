#pragma once
#include "OT/OTExtInterface.h"
#include <unordered_set>
#include "OTOracleSender.h"

using namespace libPSI;

class OTOracleReceiver :
	public OtExtReceiver
{
public:
	OTOracleReceiver(const block& seed);
	~OTOracleReceiver();
	PRNG mPrng;

	bool hasBaseOts() const override { return true; }

	void setBaseOts(
		ArrayView<std::array<block, 2>> baseSendOts) override {};
	void Extend(
		const BitVector& choices,
		ArrayView<block> messages,
		PRNG& prng,
		Channel& chl) override;

	std::unique_ptr<OtExtReceiver> split() override
	{
		std::unique_ptr<OtExtReceiver> ret(new OTOracleReceiver(mPrng.get_block()));
		return std::move(ret);
	}

};
